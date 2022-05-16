/*
 * Copyright (c) 1998, 2018, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "compiler/compileLog.hpp"
#include "interpreter/linkResolver.hpp"
#include "memory/universe.hpp"
#include "oops/objArrayKlass.hpp"
#include "opto/addnode.hpp"
#include "opto/castnode.hpp"
#include "opto/memnode.hpp"
#include "opto/parse.hpp"
#include "opto/rootnode.hpp"
#include "opto/runtime.hpp"
#include "opto/subnode.hpp"
#include "runtime/deoptimization.hpp"
#include "runtime/handles.inline.hpp"

//=============================================================================
// Helper methods for _get* and _put* bytecodes
//=============================================================================
bool Parse::static_field_ok_in_clinit(ciField *field, ciMethod *method) {
  // Could be the field_holder's <clinit> method, or <clinit> for a subklass.
  // Better to check now than to Deoptimize as soon as we execute
  assert( field->is_static(), "Only check if field is static");
  // is_being_initialized() is too generous.  It allows access to statics
  // by threads that are not running the <clinit> before the <clinit> finishes.
  // return field->holder()->is_being_initialized();

  // The following restriction is correct but conservative.
  // It is also desirable to allow compilation of methods called from <clinit>
  // but this generated code will need to be made safe for execution by
  // other threads, or the transition from interpreted to compiled code would
  // need to be guarded.
  ciInstanceKlass *field_holder = field->holder();

  if (method->holder()->is_subclass_of(field_holder)) {
    if (method->is_static_initializer()) {
      // OK to access static fields inside initializer
      return true;
    } else if (method->is_object_initializer()) {
      // It's also OK to access static fields inside a constructor,
      // because any thread calling the constructor must first have
      // synchronized on the class by executing a '_new' bytecode.
      return true;
    }
  }
  if (C->is_compiling_clinit_for(field_holder)) {
    return true; // access in the context of static initializer
  }
  return false;
}


void Parse::do_field_access(bool is_get, bool is_field) {
  bool will_link;
  ciField* field = iter().get_field(will_link);
  assert(will_link, "getfield: typeflow responsibility");

  ciInstanceKlass* field_holder = field->holder();

  if (is_field == field->is_static()) {
    // Interpreter will throw java_lang_IncompatibleClassChangeError
    // Check this before allowing <clinit> methods to access static fields
    uncommon_trap(Deoptimization::Reason_unhandled,
                  Deoptimization::Action_none);
    return;
  }

  if (!is_field && !field_holder->is_initialized()) {
    if (!static_field_ok_in_clinit(field, method())) {
      uncommon_trap(Deoptimization::Reason_uninitialized,
                    Deoptimization::Action_reinterpret,
                    NULL, "!static_field_ok_in_clinit");
      return;
    }
  }

  // Deoptimize on putfield writes to call site target field.
  if (!is_get && field->is_call_site_target()) {
    uncommon_trap(Deoptimization::Reason_unhandled,
                  Deoptimization::Action_reinterpret,
                  NULL, "put to call site target field");
    return;
  }

  assert(field->will_link(method(), bc()), "getfield: typeflow responsibility");

  // Note:  We do not check for an unloaded field type here any more.

  // Generate code for the object pointer.
  Node* obj;
  if (is_field) {
    int obj_depth = is_get ? 0 : field->type()->size();
    obj = null_check(peek(obj_depth));
    // Compile-time detect of null-exception?
    if (stopped())  return;

#ifdef ASSERT
    const TypeInstPtr *tjp = TypeInstPtr::make(TypePtr::NotNull, iter().get_declared_field_holder());
    assert(_gvn.type(obj)->higher_equal(tjp), "cast_up is no longer needed");
#endif
    // Dat mod
    Node* ac_mem_node = do_increase_access_counter(obj);

    if (is_get) {
      (void) pop();  // pop receiver before getting

      do_get_xxx(obj, field, is_field, ac_mem_node);
      // Node* ac_adr = basic_plus_adr(obj, oopDesc::access_counter_offset_in_bytes());
      // store_to_memory(control(), ac_adr, longcon(1), T_LONG, Compile::AliasIdxBot, MemNode::unordered);
    } else {

      do_put_xxx(obj, field, is_field, ac_mem_node);
      (void) pop();  // pop receiver after putting
    }
  } else {
    const TypeInstPtr* tip = TypeInstPtr::make(field_holder->java_mirror());
    obj = _gvn.makecon(tip);
    Node* ac_adr = basic_plus_adr(obj, oopDesc::access_counter_offset_in_bytes());
    // store_to_memory(control(), ac_adr, longcon(1), T_LONG, _gvn.type(ac_adr)->isa_ptr(), MemNode::unordered);
    // Dat mod
    Node* ac_mem_node = do_increase_access_counter(obj);
    if (is_get) {
      do_get_xxx(obj, field, is_field, ac_mem_node);
    } else {
      do_put_xxx(obj, field, is_field, ac_mem_node);
    }
  }
}

Node* Parse::do_increase_access_counter(Node* obj) {
  // Dat mod
  Node* ctl = control();
  Node* ac_addr = basic_plus_adr(obj, oopDesc::access_counter_offset_in_bytes());
  Node* ac_mem = memory(C->get_alias_index(TypeInstPtr::ACCESS_COUNTER));

  // Node* access_counter = LoadAccessCounterNode::make(_gvn, control(), ac_mem, ac_addr, TypeInstPtr::ACCESS_COUNTER);
  // Node* access_counter = _gvn.transform(new LoadAccessCounterNode(NULL, ac_mem, ac_addr, TypeInstPtr::ACCESS_COUNTER, TypeLong::LONG, MemNode::unordered));

  // Node* incr = _gvn.transform(new AddLNode(access_counter, longcon(1)));




  // if (require_atomic_access) {
  //   st = StoreLNode::make_atomic(ctl, mem, ac_addr, adr_type, longcon(1), mo);
  // } else {
  //   st = StoreNode::make(_gvn, ctl, mem, ac_addr, adr_type, longcon(1), T_LONG, mo);
  // }

  // st = StoreNode::make(_gvn, ctl, ac_mem, ac_addr, TypeInstPtr::ACCESS_COUNTER, longcon(1), T_LONG, MemNode::unordered);
  // st = _gvn.transform(st);
  // st->dump(0);

  // st = _gvn.transform(LoadAccessCounterNode::make(_gvn, NULL, ac_mem, ac_addr, TypeInstPtr::ACCESS_COUNTER));
  // st->dump(0);
  // assert(false, "graphkit::store_to_memory");
  // Dat mod ends
  
  // Node* st = _gvn.transform(new StoreAccessCounterNode(NULL, ac_mem, ac_addr, TypeInstPtr::ACCESS_COUNTER, longcon(1), MemNode::unordered));

  // use normal Load and store nodes
  // int adr_type = Compile::AliasIdxRaw;
  // Node* access_counter  = make_load(ctl, ac_addr, TypeLong::LONG, T_LONG, TypeInstPtr::ACCESS_COUNTER, MemNode::unordered);
  // Node* incr = _gvn.transform(new AddLNode(access_counter, longcon(1)));
  // Node* st = store_to_memory(ctl, ac_addr, incr, T_LONG, TypeInstPtr::ACCESS_COUNTER, MemNode::unordered);

  // ---------------------------------------------------
  // Node* c = make_runtime_call(RC_LEAF, OptoRuntime::modf_Type(),
  //                             CAST_FROM_FN_PTR(address, SharedRuntime::frem),
  //                             "frem", NULL, //no memory effects
  //                             f1, f2);
  make_runtime_call(RC_LEAF, OptoRuntime::print_oop_Type(),
                    CAST_FROM_FN_PTR(address, SharedRuntime::print_oop),
                    "printOop", TypeRawPtr::BOTTOM, obj);
  Node* access_counter = _gvn.transform(new LoadLNode(ctl, ac_mem, ac_addr, TypeRawPtr::BOTTOM, TypeLong::LONG, MemNode::unordered));
  Node* incr = _gvn.transform(new AddLNode(access_counter, _gvn.transform(longcon(1))));
  Node* st = _gvn.transform(new StoreLNode(ctl, ac_mem, ac_addr, TypeRawPtr::BOTTOM, incr, MemNode::unordered));
  
  st->set_is_under_investigation();

  if (PrintNodeDev) {
    st->dump(6);
    tty->print_cr("----------------------------------------");
    st->dump(-6);
  }
  // set_memory(st, TypeRawPtr::BOTTOM);
  // Back-to-back stores can only remove intermediate store with DU info
  // so push on worklist for optimizer.
  record_for_igvn(st);
  // if (ac_mem->req() > MemNode::Address && ac_addr == ac_mem->in(MemNode::Address)) {
  //   record_for_igvn(st);
  // } else {
  //   assert(false, "Should not reach here");
  // }

  make_runtime_call(RC_LEAF, OptoRuntime::print_oop_Type(),
                    CAST_FROM_FN_PTR(address, SharedRuntime::print_oop),
                    "printOop", TypeRawPtr::BOTTOM, obj);
  return st;
}


void Parse::do_get_xxx(Node* obj, ciField* field, bool is_field, Node* ac_mem_node) {
  BasicType bt = field->layout_type();

  // Does this field have a constant value?  If so, just push the value.
  if (field->is_constant() &&
      // Keep consistent with types found by ciTypeFlow: for an
      // unloaded field type, ciTypeFlow::StateVector::do_getstatic()
      // speculates the field is null. The code in the rest of this
      // method does the same. We must not bypass it and use a non
      // null constant here.
      (bt != T_OBJECT || field->type()->is_loaded())) {
    // final or stable field
    Node* con = make_constant_from_field(field, obj);
    if (con != NULL) {
      push_node(field->layout_type(), con);
      return;
    }
  }

  ciType* field_klass = field->type();
  bool is_vol = field->is_volatile();

  // Compute address and memory type.
  int offset = field->offset_in_bytes();
  const TypePtr* adr_type = C->alias_type(field)->adr_type();
  Node *adr = basic_plus_adr(obj, obj, offset);

  // Build the resultant type of the load
  const Type *type;

  bool must_assert_null = false;

  DecoratorSet decorators = IN_HEAP;
  decorators |= is_vol ? MO_SEQ_CST : MO_UNORDERED;

  bool is_obj = bt == T_OBJECT || bt == T_ARRAY;

  if (is_obj) {
    if (!field->type()->is_loaded()) {
      type = TypeInstPtr::BOTTOM;
      must_assert_null = true;
    } else if (field->is_static_constant()) {
      // This can happen if the constant oop is non-perm.
      ciObject* con = field->constant_value().as_object();
      // Do not "join" in the previous type; it doesn't add value,
      // and may yield a vacuous result if the field is of interface type.
      if (con->is_null_object()) {
        type = TypePtr::NULL_PTR;
      } else {
        type = TypeOopPtr::make_from_constant(con)->isa_oopptr();
      }
      assert(type != NULL, "field singleton type must be consistent");
    } else {
      type = TypeOopPtr::make_from_klass(field_klass->as_klass());
    }
  } else {
    type = Type::get_const_basic_type(bt);
  }

  Node* ld = access_load_at(obj, adr, adr_type, type, bt, decorators);

  // Dat mod
  // if (ac_mem_node != NULL) {
  //   ld->add_req(ac_mem_node);
  // }

  // Adjust Java stack
  if (type2size[bt] == 1)
    push(ld);
  else
    push_pair(ld);

  if (must_assert_null) {
    // Do not take a trap here.  It's possible that the program
    // will never load the field's class, and will happily see
    // null values in this field forever.  Don't stumble into a
    // trap for such a program, or we might get a long series
    // of useless recompilations.  (Or, we might load a class
    // which should not be loaded.)  If we ever see a non-null
    // value, we will then trap and recompile.  (The trap will
    // not need to mention the class index, since the class will
    // already have been loaded if we ever see a non-null value.)
    // uncommon_trap(iter().get_field_signature_index());
    if (PrintOpto && (Verbose || WizardMode)) {
      method()->print_name(); tty->print_cr(" asserting nullness of field at bci: %d", bci());
    }
    if (C->log() != NULL) {
      C->log()->elem("assert_null reason='field' klass='%d'",
                     C->log()->identify(field->type()));
    }
    // If there is going to be a trap, put it at the next bytecode:
    set_bci(iter().next_bci());
    null_assert(peek());
    set_bci(iter().cur_bci()); // put it back
  }
}

void Parse::do_put_xxx(Node* obj, ciField* field, bool is_field, Node* ac_mem_node) {
  bool is_vol = field->is_volatile();

  // Compute address and memory type.
  int offset = field->offset_in_bytes();
  const TypePtr* adr_type = C->alias_type(field)->adr_type();
  Node* adr = basic_plus_adr(obj, obj, offset);
  BasicType bt = field->layout_type();
  // Value to be stored
  Node* val = type2size[bt] == 1 ? pop() : pop_pair();

  DecoratorSet decorators = IN_HEAP;
  decorators |= is_vol ? MO_SEQ_CST : MO_UNORDERED;

  bool is_obj = bt == T_OBJECT || bt == T_ARRAY;

  // Store the value.
  const Type* field_type;
  if (!field->type()->is_loaded()) {
    field_type = TypeInstPtr::BOTTOM;
  } else {
    if (is_obj) {
      field_type = TypeOopPtr::make_from_klass(field->type()->as_klass());
    } else {
      field_type = Type::BOTTOM;
    }
  }
  Node* st = access_store_at(control(), obj, adr, adr_type, val, field_type, bt, decorators);

  // Dat mod
  // if (ac_mem_node != NULL) {
  //   st->add_req(ac_mem_node);
  // }

  if (is_field) {
    // Remember we wrote a volatile field.
    // For not multiple copy atomic cpu (ppc64) a barrier should be issued
    // in constructors which have such stores. See do_exits() in parse1.cpp.
    if (is_vol) {
      set_wrote_volatile(true);
    }
    set_wrote_fields(true);

    // If the field is final, the rules of Java say we are in <init> or <clinit>.
    // Note the presence of writes to final non-static fields, so that we
    // can insert a memory barrier later on to keep the writes from floating
    // out of the constructor.
    // Any method can write a @Stable field; insert memory barriers after those also.
    if (field->is_final()) {
      set_wrote_final(true);
      if (AllocateNode::Ideal_allocation(obj, &_gvn) != NULL) {
        // Preserve allocation ptr to create precedent edge to it in membar
        // generated on exit from constructor.
        // Can't bind stable with its allocation, only record allocation for final field.
        set_alloc_with_final(obj);
      }
    }
    if (field->is_stable()) {
      set_wrote_stable(true);
    }
  }
}

//=============================================================================
void Parse::do_anewarray() {
  bool will_link;
  ciKlass* klass = iter().get_klass(will_link);

  // Uncommon Trap when class that array contains is not loaded
  // we need the loaded class for the rest of graph; do not
  // initialize the container class (see Java spec)!!!
  assert(will_link, "anewarray: typeflow responsibility");

  ciObjArrayKlass* array_klass = ciObjArrayKlass::make(klass);
  // Check that array_klass object is loaded
  if (!array_klass->is_loaded()) {
    // Generate uncommon_trap for unloaded array_class
    uncommon_trap(Deoptimization::Reason_unloaded,
                  Deoptimization::Action_reinterpret,
                  array_klass);
    return;
  }

  kill_dead_locals();

  const TypeKlassPtr* array_klass_type = TypeKlassPtr::make(array_klass);
  Node* count_val = pop();
  Node* obj = new_array(makecon(array_klass_type), count_val, 1);
  push(obj);
}


void Parse::do_newarray(BasicType elem_type) {
  kill_dead_locals();

  Node*   count_val = pop();
  const TypeKlassPtr* array_klass = TypeKlassPtr::make(ciTypeArrayKlass::make(elem_type));
  Node*   obj = new_array(makecon(array_klass), count_val, 1);
  // Push resultant oop onto stack
  push(obj);
}

// Expand simple expressions like new int[3][5] and new Object[2][nonConLen].
// Also handle the degenerate 1-dimensional case of anewarray.
Node* Parse::expand_multianewarray(ciArrayKlass* array_klass, Node* *lengths, int ndimensions, int nargs) {
  Node* length = lengths[0];
  assert(length != NULL, "");
  Node* array = new_array(makecon(TypeKlassPtr::make(array_klass)), length, nargs);
  if (ndimensions > 1) {
    jint length_con = find_int_con(length, -1);
    guarantee(length_con >= 0, "non-constant multianewarray");
    ciArrayKlass* array_klass_1 = array_klass->as_obj_array_klass()->element_klass()->as_array_klass();
    const TypePtr* adr_type = TypeAryPtr::OOPS;
    const TypeOopPtr*    elemtype = _gvn.type(array)->is_aryptr()->elem()->make_oopptr();
    const intptr_t header   = arrayOopDesc::base_offset_in_bytes(T_OBJECT);
    for (jint i = 0; i < length_con; i++) {
      Node*    elem   = expand_multianewarray(array_klass_1, &lengths[1], ndimensions-1, nargs);
      intptr_t offset = header + ((intptr_t)i << LogBytesPerHeapOop);
      Node*    eaddr  = basic_plus_adr(array, offset);
      access_store_at(control(), array, eaddr, adr_type, elem, elemtype, T_OBJECT, IN_HEAP | IS_ARRAY);
    }
  }
  return array;
}

void Parse::do_multianewarray() {
  int ndimensions = iter().get_dimensions();

  // the m-dimensional array
  bool will_link;
  ciArrayKlass* array_klass = iter().get_klass(will_link)->as_array_klass();
  assert(will_link, "multianewarray: typeflow responsibility");

  // Note:  Array classes are always initialized; no is_initialized check.

  kill_dead_locals();

  // get the lengths from the stack (first dimension is on top)
  Node** length = NEW_RESOURCE_ARRAY(Node*, ndimensions + 1);
  length[ndimensions] = NULL;  // terminating null for make_runtime_call
  int j;
  for (j = ndimensions-1; j >= 0 ; j--) length[j] = pop();

  // The original expression was of this form: new T[length0][length1]...
  // It is often the case that the lengths are small (except the last).
  // If that happens, use the fast 1-d creator a constant number of times.
  const int expand_limit = MIN2((int)MultiArrayExpandLimit, 100);
  int expand_count = 1;        // count of allocations in the expansion
  int expand_fanout = 1;       // running total fanout
  for (j = 0; j < ndimensions-1; j++) {
    int dim_con = find_int_con(length[j], -1);
    expand_fanout *= dim_con;
    expand_count  += expand_fanout; // count the level-J sub-arrays
    if (dim_con <= 0
        || dim_con > expand_limit
        || expand_count > expand_limit) {
      expand_count = 0;
      break;
    }
  }

  // Can use multianewarray instead of [a]newarray if only one dimension,
  // or if all non-final dimensions are small constants.
  if (ndimensions == 1 || (1 <= expand_count && expand_count <= expand_limit)) {
    Node* obj = NULL;
    // Set the original stack and the reexecute bit for the interpreter
    // to reexecute the multianewarray bytecode if deoptimization happens.
    // Do it unconditionally even for one dimension multianewarray.
    // Note: the reexecute bit will be set in GraphKit::add_safepoint_edges()
    // when AllocateArray node for newarray is created.
    { PreserveReexecuteState preexecs(this);
      inc_sp(ndimensions);
      // Pass 0 as nargs since uncommon trap code does not need to restore stack.
      obj = expand_multianewarray(array_klass, &length[0], ndimensions, 0);
    } //original reexecute and sp are set back here
    push(obj);
    return;
  }

  address fun = NULL;
  switch (ndimensions) {
  case 1: ShouldNotReachHere(); break;
  case 2: fun = OptoRuntime::multianewarray2_Java(); break;
  case 3: fun = OptoRuntime::multianewarray3_Java(); break;
  case 4: fun = OptoRuntime::multianewarray4_Java(); break;
  case 5: fun = OptoRuntime::multianewarray5_Java(); break;
  };
  Node* c = NULL;

  if (fun != NULL) {
    c = make_runtime_call(RC_NO_LEAF | RC_NO_IO,
                          OptoRuntime::multianewarray_Type(ndimensions),
                          fun, NULL, TypeRawPtr::BOTTOM,
                          makecon(TypeKlassPtr::make(array_klass)),
                          length[0], length[1], length[2],
                          (ndimensions > 2) ? length[3] : NULL,
                          (ndimensions > 3) ? length[4] : NULL);
  } else {
    // Create a java array for dimension sizes
    Node* dims = NULL;
    { PreserveReexecuteState preexecs(this);
      inc_sp(ndimensions);
      Node* dims_array_klass = makecon(TypeKlassPtr::make(ciArrayKlass::make(ciType::make(T_INT))));
      dims = new_array(dims_array_klass, intcon(ndimensions), 0);

      // Fill-in it with values
      for (j = 0; j < ndimensions; j++) {
        Node *dims_elem = array_element_address(dims, intcon(j), T_INT);
        store_to_memory(control(), dims_elem, length[j], T_INT, TypeAryPtr::INTS, MemNode::unordered);
      }
    }

    c = make_runtime_call(RC_NO_LEAF | RC_NO_IO,
                          OptoRuntime::multianewarrayN_Type(),
                          OptoRuntime::multianewarrayN_Java(), NULL, TypeRawPtr::BOTTOM,
                          makecon(TypeKlassPtr::make(array_klass)),
                          dims);
  }
  make_slow_call_ex(c, env()->Throwable_klass(), false);

  Node* res = _gvn.transform(new ProjNode(c, TypeFunc::Parms));

  const Type* type = TypeOopPtr::make_from_klass_raw(array_klass);

  // Improve the type:  We know it's not null, exact, and of a given length.
  type = type->is_ptr()->cast_to_ptr_type(TypePtr::NotNull);
  type = type->is_aryptr()->cast_to_exactness(true);

  const TypeInt* ltype = _gvn.find_int_type(length[0]);
  if (ltype != NULL)
    type = type->is_aryptr()->cast_to_size(ltype);

    // We cannot sharpen the nested sub-arrays, since the top level is mutable.

  Node* cast = _gvn.transform( new CheckCastPPNode(control(), res, type) );
  push(cast);

  // Possible improvements:
  // - Make a fast path for small multi-arrays.  (W/ implicit init. loops.)
  // - Issue CastII against length[*] values, to TypeInt::POS.
}
