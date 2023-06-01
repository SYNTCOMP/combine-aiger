//
// Copyright (c) 2016, Leander Tentrup, Saarland University
//
// Licenced under ISC Licsnse, see ./LICENSE.txt form information
//

#include "combine.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Note: Input names of monitor start with string 'AIGER_NEXT_'
 */
static bool names_do_match(const char *monitor_input, const char *impl_output)
{
    const unsigned monitor_input_len = strlen(monitor_input);
    const unsigned impl_output_len = strlen(impl_output);
    const unsigned prefix_len = strlen("AIGER_NEXT_");

    if (strncmp(monitor_input, "AIGER_NEXT_", prefix_len) != 0)
    {
        // special case if input is not contained in LTL formula
        return strcmp(monitor_input, impl_output) == 0;
    }
    assert(monitor_input_len > prefix_len);

    if (prefix_len + impl_output_len > monitor_input_len)
    {
        return false;
    }
    // do a case-insensitive comparision because of different translations
    if (strcasecmp(monitor_input + prefix_len, impl_output) == 0)
    {
        return true;
    }
    return false;
}

static void check_signals_defined(const aiger *monitor, const aiger *implementation)
{
    // outputs
    for (unsigned i = 0; i < implementation->num_outputs; i++)
    {
        aiger_symbol output = implementation->outputs[i];
        bool found = false;
        for (unsigned j = 0; j < monitor->num_inputs; j++)
        {
            aiger_symbol input = monitor->inputs[j];
            if (names_do_match(input.name, output.name))
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            fprintf(stderr, "error: output %s does not appear in monitor\n", output.name);
            abort();
        }
    }

    // inputs
    for (unsigned i = 0; i < implementation->num_inputs; i++)
    {
        aiger_symbol impl_input = implementation->inputs[i];
        bool found = false;
        for (unsigned j = 0; j < monitor->num_inputs; j++)
        {
            aiger_symbol input = monitor->inputs[j];
            if (names_do_match(input.name, impl_input.name))
            {
                found = true;
                // use next literal to store corresponding literal in monitor
                impl_input.next = input.lit;
                implementation->inputs[i] = impl_input;
                break;
            }
        }
        if (!found)
        {
            fprintf(stderr, "error: input %s does not appear in monitor\n", impl_input.name);
            abort();
        }
    }
}

static void add_inputs_from_monitor(aiger *combination, const aiger *monitor, const aiger *implementation)
{
    for (unsigned i = 0; i < monitor->num_inputs; i++)
    {
        aiger_symbol input = monitor->inputs[i];
        bool is_output = false;
        for (unsigned j = 0; j < implementation->num_outputs; j++)
        {
            aiger_symbol impl_output = implementation->outputs[j];
            if (names_do_match(input.name, impl_output.name))
            {
                is_output = true;
                break;
            }
        }
        if (is_output)
        {
            continue;
        }
        aiger_add_input(combination, input.lit, input.name);
    }
}

static void add_latches_from_monitor(aiger *combination, const aiger *monitor, const aiger *implementation)
{
    for (unsigned i = 0; i < monitor->num_latches; i++)
    {
        aiger_symbol latch = monitor->latches[i];
        aiger_add_latch(combination, latch.lit, latch.next, latch.name);
        // copy reset value if present
        aiger_add_reset(combination, latch.lit, latch.reset);
    }
}

static void add_constraints_from_monitor(aiger *combination, const aiger *monitor, const aiger *implementation)
{
    // bad
    for (unsigned i = 0; i < monitor->num_bad; i++)
    {
        aiger_symbol bad = monitor->bad[i];
        aiger_add_bad(combination, bad.lit, bad.name);
    }
    // constraint
    for (unsigned i = 0; i < monitor->num_constraints; i++)
    {
        aiger_symbol constraint = monitor->constraints[i];
        aiger_add_constraint(combination, constraint.lit, constraint.name);
    }
    // justice
    for (unsigned i = 0; i < monitor->num_justice; i++)
    {
        aiger_symbol justice = monitor->justice[i];
        aiger_add_justice(combination, justice.size, justice.lits, justice.name);
    }
    // fairness
    for (unsigned i = 0; i < monitor->num_fairness; i++)
    {
        aiger_symbol fairness = monitor->fairness[i];
        aiger_add_fairness(combination, fairness.lit, fairness.name);
    }
}

static void import_ands_from_monitor(aiger *combination, const aiger *monitor)
{
    for (unsigned i = 0; i < monitor->num_ands; i++)
    {
        aiger_and and = monitor->ands[i];
        aiger_add_and(combination, and.lhs, and.rhs0, and.rhs1);
    }
}

static unsigned translate_literal(const aiger *implementation, unsigned literal, unsigned offset)
{
    unsigned tag = aiger_lit2tag(implementation, literal);
    if (tag == 0)
    {
        // constant
        return literal;
    }
    else if (tag == 1)
    {
        // input
        aiger_symbol *input = aiger_is_input(implementation, aiger_strip(literal));
        assert(input != NULL);
        assert(input->next != 0);
        return literal % 2 == 1 ? aiger_not(input->next) : input->next;
    }
    else if (tag == 2)
    {
        // latch
        return literal + offset * 2;
    }
    else
    {
        assert(tag == 3);
        // and
        return literal + offset * 2;
    }
}

static void add_latches_from_implementation(aiger *combination, const aiger *implementation, const unsigned offset)
{
    for (unsigned i = 0; i < implementation->num_latches; i++)
    {
        aiger_symbol latch = implementation->latches[i];
        unsigned int lit = latch.lit;
        // Reset value is only valid if reset = 0 || reset == 1 || reset == lit
        // lit might get changed during the translation so if it was the same before, it should be the same afterwords
        latch.lit = translate_literal(implementation, latch.lit, offset);
        latch.next = translate_literal(implementation, latch.next, offset);
        aiger_add_latch(combination, latch.lit, latch.next, latch.name);

        // copy reset value if present (if before lit == reset, then the reset is also translated)
        aiger_add_reset(combination, latch.lit, (lit == latch.reset) ? latch.lit : latch.reset);
    }
}

static void import_ands_from_implementation(aiger *combination, const aiger *implementation, const unsigned offset)
{
    for (unsigned i = 0; i < implementation->num_ands; i++)
    {
        aiger_and and = implementation->ands[i];
        and.lhs = translate_literal(implementation, and.lhs, offset);
        and.rhs0 = translate_literal(implementation, and.rhs0, offset);
        and.rhs1 = translate_literal(implementation, and.rhs1, offset);
        aiger_add_and(combination, and.lhs, and.rhs0, and.rhs1);
    }
}

static void define_outputs(aiger *combination, const aiger *monitor, const aiger *implementation, const unsigned offset)
{
    for (unsigned i = 0; i < monitor->num_inputs; i++)
    {
        aiger_symbol input = monitor->inputs[i];
        bool is_output = false;
        aiger_symbol output;
        for (unsigned j = 0; j < implementation->num_outputs; j++)
        {
            output = implementation->outputs[j];
            if (names_do_match(input.name, output.name))
            {
                is_output = true;
                break;
            }
        }
        if (!is_output)
        {
            continue;
        }
        unsigned function = translate_literal(implementation, output.lit, offset);
        aiger_add_and(combination, input.lit, function, 1);
    }
}

aiger *combine(const aiger *monitor, const aiger *implementation)
{
    aiger *combination = aiger_init();
    if (combination == NULL)
    {
        return NULL;
    }

    const unsigned offset = monitor->maxvar + 1;

    check_signals_defined(monitor, implementation);
    add_inputs_from_monitor(combination, monitor, implementation);
    add_latches_from_monitor(combination, monitor, implementation);
    assert(monitor->num_outputs == 0);
    add_constraints_from_monitor(combination, monitor, implementation);
    import_ands_from_monitor(combination, monitor);

    add_latches_from_implementation(combination, implementation, offset);
    import_ands_from_implementation(combination, implementation, offset);
    define_outputs(combination, monitor, implementation, offset);
    return combination;
}
