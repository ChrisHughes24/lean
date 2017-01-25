/*
Copyright (c) 2016 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include "kernel/instantiate.h"
#include "library/placeholder.h"
#include "library/explicit.h"
#include "library/vm/vm.h"
#include "library/vm/vm_expr.h"

namespace lean {
vm_obj pexpr_subst(vm_obj const & _e1, vm_obj const & _e2) {
    expr const & e1 = to_expr(_e1);
    expr const & e2 = to_expr(_e2);
    if (is_lambda(e1)) {
        return to_obj(instantiate(binding_body(e1), e2));
    } else {
        return to_obj(e1);
    }
}

vm_obj pexpr_of_expr(vm_obj const & e) {
    return to_obj(mk_as_is(to_expr(e)));
}

vm_obj expr_to_string(vm_obj const &);

vm_obj pexpr_to_string(vm_obj const & e) {
    return expr_to_string(e);
}

vm_obj pexpr_to_raw_expr(vm_obj const & e) {
    return e;
}

vm_obj pexpr_of_raw_expr(vm_obj const & e) {
    return e;
}

vm_obj pexpr_mk_placeholder() {
    return to_obj(mk_expr_placeholder());
}

void initialize_vm_pexpr() {
    DECLARE_VM_BUILTIN(name({"pexpr", "subst"}),          pexpr_subst);
    DECLARE_VM_BUILTIN(name({"pexpr", "of_expr"}),        pexpr_of_expr);
    DECLARE_VM_BUILTIN(name({"pexpr", "to_string"}),      pexpr_to_string);
    DECLARE_VM_BUILTIN(name({"pexpr", "of_raw_expr"}),    pexpr_of_raw_expr);
    DECLARE_VM_BUILTIN(name({"pexpr", "to_raw_expr"}),    pexpr_to_raw_expr);
    DECLARE_VM_BUILTIN(name({"pexpr", "mk_placeholder"}), pexpr_mk_placeholder);
}

void finalize_vm_pexpr() {
}
}
