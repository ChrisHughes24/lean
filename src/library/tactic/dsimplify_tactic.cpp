/*
Copyright (c) 2016 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include "util/interrupt.h"
#include "kernel/instantiate.h"
#include "library/trace.h"
#include "library/simp_lemmas.h"
#include "library/type_context.h"
#include "library/defeq_canonizer.h"
#include "library/fun_info.h"

namespace lean {
class dsimplify_core_fn {
protected:
    type_context &        m_ctx;
    expr_struct_map<expr> m_cache;
    unsigned              m_num_steps;
    bool                  m_need_restart;

    unsigned              m_max_steps;
    bool                  m_visit_instances;

    virtual optional<pair<expr, bool>> pre(expr const &) {
        return optional<pair<expr, bool>>();
    }

    virtual optional<pair<expr, bool>> post(expr const &) {
        return optional<pair<expr, bool>>();
    }

    expr visit_macro(expr const & e) {
        buffer<expr> new_args;
        for (unsigned i = 0; i < macro_num_args(e); i++)
            new_args.push_back(visit(macro_arg(e, i)));
        return update_macro(e, new_args.size(), new_args.data());
    }

    expr visit_binding(expr const & e) {
        expr_kind k = e.kind();
        type_context::tmp_locals locals(m_ctx);
        expr b = e;
        bool modified = false;
        while (b.kind() == k) {
            expr d = instantiate_rev(binding_domain(b), locals.size(), locals.data());
            expr new_d = visit(d);
            if (!is_eqp(d, new_d)) modified = true;
            locals.push_local(binding_name(b), new_d, binding_info(b));
            b = binding_body(b);
        }
        b = instantiate_rev(b, locals.size(), locals.data());
        expr new_b = visit(b);
        if (!is_eqp(b, new_b)) modified = true;
        if (modified)
            return k == expr_kind::Pi ? locals.mk_pi(new_b) : locals.mk_lambda(new_b);
        else
            return e;
    }

    expr visit_let(expr const & e) {
        type_context::tmp_locals locals(m_ctx);
        expr b = e;
        bool modified = false;
        while (is_let(b)) {
            expr t     = instantiate_rev(let_type(b), locals.size(), locals.data());
            expr v     = instantiate_rev(let_value(b), locals.size(), locals.data());
            expr new_t = visit(t);
            expr new_v = visit(v);
            if (!is_eqp(t, new_t) || !is_eqp(v, new_v)) modified = true;
            locals.push_let(let_name(b), t, v);
            b = let_body(b);
        }
        b = instantiate_rev(b, locals.size(), locals.data());
        expr new_b = visit(b);
        if (!is_eqp(b, new_b)) modified = true;
        if (modified)
            return locals.mk_lambda(new_b);
        else
            return e;
    }

    expr visit_app(expr const & e) {
        buffer<expr> args;
        bool modified = false;
        expr f        = get_app_args(e, args);
        unsigned i    = 0;
        if (!m_visit_instances) {
            fun_info info = get_fun_info(m_ctx, f, args.size());
            for (param_info const & pinfo : info.get_params_info()) {
                lean_assert(i < args.size());
                expr new_a;
                if (pinfo.is_inst_implicit()) {
                    new_a = defeq_canonize(m_ctx, args[i], m_need_restart);
                } else {
                    new_a = visit(args[i]);
                }
                if (new_a != args[i])
                    modified = true;
                args[i] = new_a;
                i++;
            }
        }
        for (; i < args.size(); i++) {
            expr new_a = visit(args[i]);
            if (new_a != args[i])
                modified = true;
            args[i] = new_a;
        }
        if (modified)
            return mk_app(f, args);
        else
            return e;
    }

    void inc_num_steps() {
        m_num_steps++;
        if (m_num_steps > m_max_steps)
            throw exception("dsimplify failed, maximum number of steps exceeded");
    }

    expr visit(expr const & e) {
        check_system("dsimplify");
        lean_trace_inc_depth("dsimplify");
        lean_trace_d("dsimplify", scope_trace_env scope(m_ctx.env(), m_ctx); tout() << e << "\n";);
        inc_num_steps();

        auto it = m_cache.find(e);
        if (it != m_cache.end())
            return it->second;

        if (auto p1 = pre(e)) {
            if (!p1->second) {
                m_cache.insert(mk_pair(e, p1->first));
                return p1->first;
            }
        }

        expr curr_e = e;
        while (true) {
            expr new_e;
            switch (curr_e.kind()) {
            case expr_kind::Local:
            case expr_kind::Meta:
            case expr_kind::Sort:
            case expr_kind::Constant:
                new_e = curr_e;
                break;
            case expr_kind::Var:
                lean_unreachable();
            case expr_kind::Macro:
                new_e = visit_macro(curr_e);
                break;
            case expr_kind::Lambda:
            case expr_kind::Pi:
                new_e = visit_binding(curr_e);
                break;
            case expr_kind::App:
                new_e = visit_app(curr_e);
                break;
            case expr_kind::Let:
                new_e = visit_let(curr_e);
                break;
            }

            if (auto p2 = post(new_e)) {
                curr_e = p2->first;
                if (!p2->second)
                    break;
            } else {
                break;
            }
        }
        m_cache.insert(mk_pair(e, curr_e));
        return curr_e;
    }
public:
    dsimplify_core_fn(type_context & ctx, unsigned max_steps, bool visit_instances):
        m_ctx(ctx), m_num_steps(0), m_need_restart(false),
        m_max_steps(max_steps), m_visit_instances(visit_instances) {}

    expr operator()(expr e) {
        while (true) {
            m_need_restart = false;
            e = visit(e);
            if (!m_need_restart)
                return e;
            m_cache.clear();
        }
    }
};

class dsimplify_fn : public dsimplify_core_fn {
    simp_lemmas_for m_simp_lemmas;

    virtual optional<pair<expr, bool>> post(expr const & e) override {
        expr curr_e = e;
        while (true) {
            check_system("dsimplify");
            inc_num_steps();
            list<simp_lemma> const * simp_lemmas_ptr = m_simp_lemmas.find(curr_e);
            if (!simp_lemmas_ptr) break;
            buffer<simp_lemma> simp_lemmas;
            to_buffer(*simp_lemmas_ptr, simp_lemmas);

            expr new_e = curr_e;
            for (simp_lemma const & sl : simp_lemmas) {
                if (sl.is_refl()) {
                    new_e = refl_lemma_rewrite(m_ctx, new_e, sl);
                    break;
                }
            }
            if (new_e == curr_e) break;
            curr_e = new_e;
        }
        if (curr_e == e)
            return optional<pair<expr, bool>>();
        else
            return optional<pair<expr, bool>>(curr_e, true);
    }
public:
    dsimplify_fn(type_context & ctx, simp_lemmas_for const & lemmas, unsigned max_steps, bool visit_instances):
        dsimplify_core_fn(ctx, max_steps, visit_instances),
        m_simp_lemmas(lemmas) {
    }
};
}