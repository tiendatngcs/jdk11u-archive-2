/*
 * Copyright (c) 2017, 2018, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_OOPS_ACCESS_INLINE_HPP
#define SHARE_OOPS_ACCESS_INLINE_HPP

#include "gc/shared/barrierSet.inline.hpp"
#include "gc/shared/barrierSetConfig.inline.hpp"
#include "oops/access.hpp"
#include "oops/accessBackend.inline.hpp"

// This file outlines the last 2 steps of the template pipeline of accesses going through
// the Access API.
// * Step 5.a: Barrier resolution. This step is invoked the first time a runtime-dispatch
//             happens for an access. The appropriate BarrierSet::AccessBarrier accessor
//             is resolved, then the function pointer is updated to that accessor for
//             future invocations.
// * Step 5.b: Post-runtime dispatch. This step now casts previously unknown types such
//             as the address type of an oop on the heap (is it oop* or narrowOop*) to
//             the appropriate type. It also splits sufficiently orthogonal accesses into
//             different functions, such as whether the access involves oops or primitives
//             and whether the access is performed on the heap or outside. Then the
//             appropriate BarrierSet::AccessBarrier is called to perform the access.

namespace AccessInternal {
  // Step 5.b: Post-runtime dispatch.
  // This class is the last step before calling the BarrierSet::AccessBarrier.
  // Here we make sure to figure out types that were not known prior to the
  // runtime dispatch, such as whether an oop on the heap is oop or narrowOop.
  // We also split orthogonal barriers such as handling primitives vs oops
  // and on-heap vs off-heap into different calls to the barrier set.
  template <class GCBarrierType, BarrierType type, DecoratorSet decorators>
  struct PostRuntimeDispatch: public AllStatic { };

  template <class GCBarrierType, DecoratorSet decorators>
  struct PostRuntimeDispatch<GCBarrierType, BARRIER_STORE, decorators>: public AllStatic {
    template <typename T>
    static void access_barrier(void* addr, T value) {
      tty->print_cr("store_in_heap");
      GCBarrierType::store_in_heap(reinterpret_cast<T*>(addr), value);
    }

    static void oop_access_barrier(void* addr, oop value) {
      typedef typename HeapOopType<decorators>::type OopType;
      tty->print_cr("oop_store_in_heap oop value ac %lu | gc_epoch %lu", value->access_counter(), value->gc_epoch());
      if (HasDecorator<decorators, IN_HEAP>::value) {
        GCBarrierType::oop_store_in_heap(reinterpret_cast<OopType*>(addr), value);
      } else {
        GCBarrierType::oop_store_not_in_heap(reinterpret_cast<OopType*>(addr), value);
      }
    }
  };

  template <class GCBarrierType, DecoratorSet decorators>
  struct PostRuntimeDispatch<GCBarrierType, BARRIER_LOAD, decorators>: public AllStatic {
    template <typename T>
    static T access_barrier(void* addr) {
      tty->print_cr("load_in_heap");
      return GCBarrierType::load_in_heap(reinterpret_cast<T*>(addr));
    }

    static oop oop_access_barrier(void* addr) {
      typedef typename HeapOopType<decorators>::type OopType;
      oop returning_oop = NULL;
      if (HasDecorator<decorators, IN_HEAP>::value) {
        returning_oop = GCBarrierType::oop_load_in_heap(reinterpret_cast<OopType*>(addr));
      } else {
        returning_oop = GCBarrierType::oop_load_not_in_heap(reinterpret_cast<OopType*>(addr));
      }
      tty->print_cr("oop_load_in_heap oop returning_oop ac %lu | gc_epoch %lu", returning_oop->access_counter(), returning_oop->gc_epoch());
      return returning_oop;
    }
  };

  template <class GCBarrierType, DecoratorSet decorators>
  struct PostRuntimeDispatch<GCBarrierType, BARRIER_ATOMIC_XCHG, decorators>: public AllStatic {
    template <typename T>
    static T access_barrier(T new_value, void* addr) {
      tty->print_cr("atomic_xchg_in_heap");
      return GCBarrierType::atomic_xchg_in_heap(new_value, reinterpret_cast<T*>(addr));
    }

    static oop oop_access_barrier(oop new_value, void* addr) {
      typedef typename HeapOopType<decorators>::type OopType;
      oop returning_oop = NULL;
      if (HasDecorator<decorators, IN_HEAP>::value) {
        returning_oop = GCBarrierType::oop_atomic_xchg_in_heap(new_value, reinterpret_cast<OopType*>(addr));
      } else {
        returning_oop = GCBarrierType::oop_atomic_xchg_not_in_heap(new_value, reinterpret_cast<OopType*>(addr));
      }
      tty->print_cr("oop_atomic_xchg_in_heap oop returning_oop ac %lu | gc_epoch %lu", returning_oop->access_counter(), returning_oop->gc_epoch());
      tty->print_cr("oop_atomic_xchg_in_heap oop new_value ac %lu | gc_epoch %lu", new_value->access_counter(), new_value->gc_epoch());
      return returning_oop;
    }
  };

  template <class GCBarrierType, DecoratorSet decorators>
  struct PostRuntimeDispatch<GCBarrierType, BARRIER_ATOMIC_CMPXCHG, decorators>: public AllStatic {
    template <typename T>
    static T access_barrier(T new_value, void* addr, T compare_value) {
      tty->print_cr("atomic_cmpxchg_in_heap");
      return GCBarrierType::atomic_cmpxchg_in_heap(new_value, reinterpret_cast<T*>(addr), compare_value);
    }

    static oop oop_access_barrier(oop new_value, void* addr, oop compare_value) {
      typedef typename HeapOopType<decorators>::type OopType;
      oop returning_oop = NULL;
      if (HasDecorator<decorators, IN_HEAP>::value) {
        returning_oop = GCBarrierType::oop_atomic_cmpxchg_in_heap(new_value, reinterpret_cast<OopType*>(addr), compare_value);
      } else {
        returning_oop = GCBarrierType::oop_atomic_cmpxchg_not_in_heap(new_value, reinterpret_cast<OopType*>(addr), compare_value);
      }
      tty->print_cr("oop_atomic_xchg_in_heap oop returning_oop ac %lu | gc_epoch %lu", returning_oop->access_counter(), returning_oop->gc_epoch());
      tty->print_cr("oop_atomic_xchg_in_heap oop new_value ac %lu | gc_epoch %lu", new_value->access_counter(), new_value->gc_epoch());
      return returning_oop;
    }
  };

  template <class GCBarrierType, DecoratorSet decorators>
  struct PostRuntimeDispatch<GCBarrierType, BARRIER_ARRAYCOPY, decorators>: public AllStatic {
    template <typename T>
    static bool access_barrier(arrayOop src_obj, size_t src_offset_in_bytes, T* src_raw,
                               arrayOop dst_obj, size_t dst_offset_in_bytes, T* dst_raw,
                               size_t length) {
      tty->print_cr("arraycopy_in_heap oop src_obj ac %lu | gc_epoch %lu", src_obj->access_counter(), src_obj->gc_epoch());
      tty->print_cr("arraycopy_in_heap oop dst_obj ac %lu | gc_epoch %lu", dst_obj->access_counter(), dst_obj->gc_epoch());
      src_obj->increase_access_counter();
      dst_obj->increase_access_counter();
      GCBarrierType::arraycopy_in_heap(src_obj, src_offset_in_bytes, src_raw,
                                       dst_obj, dst_offset_in_bytes, dst_raw,
                                       length);
      return true;
    }

    template <typename T>
    static bool oop_access_barrier(arrayOop src_obj, size_t src_offset_in_bytes, T* src_raw,
                                   arrayOop dst_obj, size_t dst_offset_in_bytes, T* dst_raw,
                                   size_t length) {
      typedef typename HeapOopType<decorators>::type OopType;
      tty->print_cr("oop_arraycopy_in_heap oop src_obj ac %lu | gc_epoch %lu", src_obj->access_counter(), src_obj->gc_epoch());
      tty->print_cr("oop_arraycopy_in_heap oop dst_obj ac %lu | gc_epoch %lu", dst_obj->access_counter(), dst_obj->gc_epoch());
      src_obj->increase_access_counter();
      dst_obj->increase_access_counter();
      return GCBarrierType::oop_arraycopy_in_heap(src_obj, src_offset_in_bytes, reinterpret_cast<OopType*>(src_raw),
                                                  dst_obj, dst_offset_in_bytes, reinterpret_cast<OopType*>(dst_raw),
                                                  length);
    }
  };

  template <class GCBarrierType, DecoratorSet decorators>
  struct PostRuntimeDispatch<GCBarrierType, BARRIER_STORE_AT, decorators>: public AllStatic {
    template <typename T>
    static void access_barrier(oop base, ptrdiff_t offset, T value) {
      tty->print_cr("store_in_heap_at oop base ac %lu | gc_epoch %lu", base->access_counter(), base->gc_epoch());
      base->increase_access_counter();
      GCBarrierType::store_in_heap_at(base, offset, value);
    }

    static void oop_access_barrier(oop base, ptrdiff_t offset, oop value) {
      tty->print_cr("oop_store_in_heap_at oop base ac %lu | gc_epoch %lu", base->access_counter(), base->gc_epoch());
      tty->print_cr("oop_store_in_heap_at oop value ac %lu | gc_epoch %lu", value->access_counter(), value->gc_epoch());
      base->increase_access_counter();
      GCBarrierType::oop_store_in_heap_at(base, offset, value);
    }
  };

  template <class GCBarrierType, DecoratorSet decorators>
  struct PostRuntimeDispatch<GCBarrierType, BARRIER_LOAD_AT, decorators>: public AllStatic {
    template <typename T>
    static T access_barrier(oop base, ptrdiff_t offset) {
      tty->print_cr("load_in_heap_at oop base ac %lu | gc_epoch %lu", base->access_counter(), base->gc_epoch());
      base->increase_access_counter();
      return GCBarrierType::template load_in_heap_at<T>(base, offset);
    }

    static oop oop_access_barrier(oop base, ptrdiff_t offset) {
      tty->print_cr("oop_load_in_heap_at oop base ac %lu | gc_epoch %lu", base->access_counter(), base->gc_epoch());
      base->increase_access_counter();
      oop returning_oop = GCBarrierType::oop_load_in_heap_at(base, offset);
      tty->print_cr("oop_load_in_heap_at oop returning_oop ac %lu | gc_epoch %lu", returning_oop->access_counter(), returning_oop->gc_epoch());
      return returning_oop;
    }
  };

  template <class GCBarrierType, DecoratorSet decorators>
  struct PostRuntimeDispatch<GCBarrierType, BARRIER_ATOMIC_XCHG_AT, decorators>: public AllStatic {
    template <typename T>
    static T access_barrier(T new_value, oop base, ptrdiff_t offset) {
      tty->print_cr("atomic_xchg_in_heap_at oop base ac %lu | gc_epoch %lu", base->access_counter(), base->gc_epoch());
      base->increase_access_counter();
      return GCBarrierType::atomic_xchg_in_heap_at(new_value, base, offset);
    }

    static oop oop_access_barrier(oop new_value, oop base, ptrdiff_t offset) {
      tty->print_cr("oop_atomic_xchg_in_heap_at oop base ac %lu | gc_epoch %lu", base->access_counter(), base->gc_epoch());
      base->increase_access_counter();
      oop returning_oop = GCBarrierType::oop_atomic_xchg_in_heap_at(new_value, base, offset);
      tty->print_cr("oop_atomic_xchg_in_heap_at oop returning_oop ac %lu | gc_epoch %lu", returning_oop->access_counter(), returning_oop->gc_epoch());
      return returning_oop;
    }
  };

  template <class GCBarrierType, DecoratorSet decorators>
  struct PostRuntimeDispatch<GCBarrierType, BARRIER_ATOMIC_CMPXCHG_AT, decorators>: public AllStatic {
    template <typename T>
    static T access_barrier(T new_value, oop base, ptrdiff_t offset, T compare_value) {
      tty->print_cr("atomic_cmpxchg_in_heap_at oop base ac %lu | gc_epoch %lu", base->access_counter(), base->gc_epoch());
      base->increase_access_counter();
      return GCBarrierType::atomic_cmpxchg_in_heap_at(new_value, base, offset, compare_value);
    }

    static oop oop_access_barrier(oop new_value, oop base, ptrdiff_t offset, oop compare_value) {
      tty->print_cr("oop_atomic_cmpxchg_in_heap_at oop base ac %lu | gc_epoch %lu", base->access_counter(), base->gc_epoch());
      base->increase_access_counter();
      oop returning_oop = GCBarrierType::oop_atomic_cmpxchg_in_heap_at(new_value, base, offset, compare_value);
      tty->print_cr("oop_atomic_cmpxchg_in_heap_at oop returning_oop ac %lu | gc_epoch %lu", returning_oop->access_counter(), returning_oop->gc_epoch());
      return returning_oop;
    }
  };

  template <class GCBarrierType, DecoratorSet decorators>
  struct PostRuntimeDispatch<GCBarrierType, BARRIER_CLONE, decorators>: public AllStatic {
    static void access_barrier(oop src, oop dst, size_t size) {
      tty->print_cr("clone_in_heap oop src ac %lu | gc_epoch %lu", src->access_counter(), src->gc_epoch());
      tty->print_cr("clone_in_heap oop value ac %lu | gc_epoch %lu", dst->access_counter(), dst->gc_epoch());
      src->increase_access_counter();
      dst->increase_access_counter();
      GCBarrierType::clone_in_heap(src, dst, size);
    }
  };

  template <class GCBarrierType, DecoratorSet decorators>
  struct PostRuntimeDispatch<GCBarrierType, BARRIER_RESOLVE, decorators>: public AllStatic {
    static oop access_barrier(oop obj) {
      tty->print_cr("resolve oop obj ac %lu | gc_epoch %lu", obj->access_counter(), obj->gc_epoch());
      obj->increase_access_counter();
      oop returning_oop = GCBarrierType::resolve(obj);
      tty->print_cr("resolve oop returning_oop ac %lu | gc_epoch %lu", returning_oop->access_counter(), returning_oop->gc_epoch());
      return returning_oop;
    }
  };

  // Resolving accessors with barriers from the barrier set happens in two steps.
  // 1. Expand paths with runtime-decorators, e.g. is UseCompressedOops on or off.
  // 2. Expand paths for each BarrierSet available in the system.
  template <DecoratorSet decorators, typename FunctionPointerT, BarrierType barrier_type>
  struct BarrierResolver: public AllStatic {
    template <DecoratorSet ds>
    static typename EnableIf<
      HasDecorator<ds, INTERNAL_VALUE_IS_OOP>::value,
      FunctionPointerT>::type
    resolve_barrier_gc() {
      BarrierSet* bs = BarrierSet::barrier_set();
      assert(bs != NULL, "GC barriers invoked before BarrierSet is set");
      switch (bs->kind()) {
#define BARRIER_SET_RESOLVE_BARRIER_CLOSURE(bs_name)                    \
        case BarrierSet::bs_name: {                                     \
          return PostRuntimeDispatch<typename BarrierSet::GetType<BarrierSet::bs_name>::type:: \
            AccessBarrier<ds>, barrier_type, ds>::oop_access_barrier; \
        }                                                               \
        break;
        FOR_EACH_CONCRETE_BARRIER_SET_DO(BARRIER_SET_RESOLVE_BARRIER_CLOSURE)
#undef BARRIER_SET_RESOLVE_BARRIER_CLOSURE

      default:
        fatal("BarrierSet AccessBarrier resolving not implemented");
        return NULL;
      };
    }

    template <DecoratorSet ds>
    static typename EnableIf<
      !HasDecorator<ds, INTERNAL_VALUE_IS_OOP>::value,
      FunctionPointerT>::type
    resolve_barrier_gc() {
      BarrierSet* bs = BarrierSet::barrier_set();
      assert(bs != NULL, "GC barriers invoked before BarrierSet is set");
      switch (bs->kind()) {
#define BARRIER_SET_RESOLVE_BARRIER_CLOSURE(bs_name)                    \
        case BarrierSet::bs_name: {                                       \
          return PostRuntimeDispatch<typename BarrierSet::GetType<BarrierSet::bs_name>::type:: \
            AccessBarrier<ds>, barrier_type, ds>::access_barrier; \
        }                                                                 \
        break;
        FOR_EACH_CONCRETE_BARRIER_SET_DO(BARRIER_SET_RESOLVE_BARRIER_CLOSURE)
#undef BARRIER_SET_RESOLVE_BARRIER_CLOSURE

      default:
        fatal("BarrierSet AccessBarrier resolving not implemented");
        return NULL;
      };
    }

    static FunctionPointerT resolve_barrier_rt() {
      if (UseCompressedOops) {
        const DecoratorSet expanded_decorators = decorators | INTERNAL_RT_USE_COMPRESSED_OOPS;
        return resolve_barrier_gc<expanded_decorators>();
      } else {
        return resolve_barrier_gc<decorators>();
      }
    }

    static FunctionPointerT resolve_barrier() {
      return resolve_barrier_rt();
    }
  };

  // Step 5.a: Barrier resolution
  // The RuntimeDispatch class is responsible for performing a runtime dispatch of the
  // accessor. This is required when the access either depends on whether compressed oops
  // is being used, or it depends on which GC implementation was chosen (e.g. requires GC
  // barriers). The way it works is that a function pointer initially pointing to an
  // accessor resolution function gets called for each access. Upon first invocation,
  // it resolves which accessor to be used in future invocations and patches the
  // function pointer to this new accessor.

  template <DecoratorSet decorators, typename T>
  void RuntimeDispatch<decorators, T, BARRIER_STORE>::store_init(void* addr, T value) {
    func_t function = BarrierResolver<decorators, func_t, BARRIER_STORE>::resolve_barrier();
    _store_func = function;
    function(addr, value);
  }

  template <DecoratorSet decorators, typename T>
  void RuntimeDispatch<decorators, T, BARRIER_STORE_AT>::store_at_init(oop base, ptrdiff_t offset, T value) {
    func_t function = BarrierResolver<decorators, func_t, BARRIER_STORE_AT>::resolve_barrier();
    _store_at_func = function;
    function(base, offset, value);
  }

  template <DecoratorSet decorators, typename T>
  T RuntimeDispatch<decorators, T, BARRIER_LOAD>::load_init(void* addr) {
    func_t function = BarrierResolver<decorators, func_t, BARRIER_LOAD>::resolve_barrier();
    _load_func = function;
    return function(addr);
  }

  template <DecoratorSet decorators, typename T>
  T RuntimeDispatch<decorators, T, BARRIER_LOAD_AT>::load_at_init(oop base, ptrdiff_t offset) {
    func_t function = BarrierResolver<decorators, func_t, BARRIER_LOAD_AT>::resolve_barrier();
    _load_at_func = function;
    return function(base, offset);
  }

  template <DecoratorSet decorators, typename T>
  T RuntimeDispatch<decorators, T, BARRIER_ATOMIC_CMPXCHG>::atomic_cmpxchg_init(T new_value, void* addr, T compare_value) {
    func_t function = BarrierResolver<decorators, func_t, BARRIER_ATOMIC_CMPXCHG>::resolve_barrier();
    _atomic_cmpxchg_func = function;
    return function(new_value, addr, compare_value);
  }

  template <DecoratorSet decorators, typename T>
  T RuntimeDispatch<decorators, T, BARRIER_ATOMIC_CMPXCHG_AT>::atomic_cmpxchg_at_init(T new_value, oop base, ptrdiff_t offset, T compare_value) {
    func_t function = BarrierResolver<decorators, func_t, BARRIER_ATOMIC_CMPXCHG_AT>::resolve_barrier();
    _atomic_cmpxchg_at_func = function;
    return function(new_value, base, offset, compare_value);
  }

  template <DecoratorSet decorators, typename T>
  T RuntimeDispatch<decorators, T, BARRIER_ATOMIC_XCHG>::atomic_xchg_init(T new_value, void* addr) {
    func_t function = BarrierResolver<decorators, func_t, BARRIER_ATOMIC_XCHG>::resolve_barrier();
    _atomic_xchg_func = function;
    return function(new_value, addr);
  }

  template <DecoratorSet decorators, typename T>
  T RuntimeDispatch<decorators, T, BARRIER_ATOMIC_XCHG_AT>::atomic_xchg_at_init(T new_value, oop base, ptrdiff_t offset) {
    func_t function = BarrierResolver<decorators, func_t, BARRIER_ATOMIC_XCHG_AT>::resolve_barrier();
    _atomic_xchg_at_func = function;
    return function(new_value, base, offset);
  }

  template <DecoratorSet decorators, typename T>
  bool RuntimeDispatch<decorators, T, BARRIER_ARRAYCOPY>::arraycopy_init(arrayOop src_obj, size_t src_offset_in_bytes, T* src_raw,
                                                                         arrayOop dst_obj, size_t dst_offset_in_bytes, T* dst_raw,
                                                                         size_t length) {
    func_t function = BarrierResolver<decorators, func_t, BARRIER_ARRAYCOPY>::resolve_barrier();
    _arraycopy_func = function;
    return function(src_obj, src_offset_in_bytes, src_raw,
                    dst_obj, dst_offset_in_bytes, dst_raw,
                    length);
  }

  template <DecoratorSet decorators, typename T>
  void RuntimeDispatch<decorators, T, BARRIER_CLONE>::clone_init(oop src, oop dst, size_t size) {
    func_t function = BarrierResolver<decorators, func_t, BARRIER_CLONE>::resolve_barrier();
    _clone_func = function;
    function(src, dst, size);
  }

  template <DecoratorSet decorators, typename T>
  oop RuntimeDispatch<decorators, T, BARRIER_RESOLVE>::resolve_init(oop obj) {
    func_t function = BarrierResolver<decorators, func_t, BARRIER_RESOLVE>::resolve_barrier();
    _resolve_func = function;
    return function(obj);
  }
}

#endif // SHARE_OOPS_ACCESS_INLINE_HPP
