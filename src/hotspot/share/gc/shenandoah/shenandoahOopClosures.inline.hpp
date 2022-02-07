/*
 * Copyright (c) 2015, 2018, Red Hat, Inc. All rights reserved.
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

#ifndef SHARE_VM_GC_SHENANDOAH_SHENANDOAHOOPCLOSURES_INLINE_HPP
#define SHARE_VM_GC_SHENANDOAH_SHENANDOAHOOPCLOSURES_INLINE_HPP

#include "gc/shenandoah/shenandoahHeap.inline.hpp"
#include "gc/shenandoah/shenandoahConcurrentMark.inline.hpp"

template<class T, UpdateRefsMode UPDATE_REFS, StringDedupMode STRING_DEDUP>
inline void ShenandoahMarkRefsSuperClosure::work(T *p) {
  ShenandoahConcurrentMark::mark_through_ref<T, UPDATE_REFS, STRING_DEDUP>(p, _heap, _queue, _mark_context);
}

template <class T>
inline void ShenandoahUpdateHeapRefsClosure::do_oop_work(T* p) {
  _heap->maybe_update_with_forwarded(p);
}

template <class T>
inline void ShenandoahStatsCollectionClosure::do_oop_work(T *p) {
  T o = RawAccess<>::oop_load(p);
  if (!CompressedOops::is_null(o)) {
    oop obj = CompressedOops::decode_not_null(o);
    tty->print_cr("Printing oop ac = %lu | gc_epoch = %lu | region = %lu", obj->access_counter(), obj->gc_epoch(), _heap->heap_region_index_containing(obj));
  }
}

#endif // SHARE_VM_GC_SHENANDOAH_SHENANDOAHOOPCLOSURES_INLINE_HPP
