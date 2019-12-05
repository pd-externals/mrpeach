
/* sprint started 20191205 by Martin Peach chakekatzil@gmail.com based on print from x_interface.c */

#include "m_pd.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
/* -------------------------- sprint ------------------------------ */
static t_class *sprint_class;

typedef struct _sprint
{
    t_object    x_obj;
    t_symbol    *x_sym;
    int         x_separator;
    int         x_len; // length of string in x_buf
    char        x_buf[MAXPDSTRING];
} t_sprint;

static void *sprint_new(t_symbol *sel, int argc, t_atom *argv)
{
    int i;

    t_sprint *x = (t_sprint *)pd_new(sprint_class);
    if (argc == 0)
    {
        x->x_sym = gensym("sprint");
        x->x_separator = -1; // = 'no separator'
    }
    else if (argc >= 1 && argv->a_type == A_SYMBOL)
    {
        t_symbol *s = atom_getsymbolarg(0, argc, argv);
        if (1 == strlen(s->s_name)) x->x_separator = s->s_name[0];
    }
    else if (argc >= 1 && argv->a_type == A_FLOAT)
    {
        float f = argv->a_w.w_float;
        int i = f;
        if ((f == i) && (i >= 0) && (i < 256)) x->x_separator = i; // some 8-bit value
    }
    outlet_new(&x->x_obj, &s_float);
    x->x_len = 0;
    return (x);
}

/* sprint_drip outputs x_buf one character at a time as floats */
void sprint_drip(t_sprint *x)
{
    int i;
    char c;
    float f;

    for (i = 0; i < x->x_len; ++i)
    {
        c = x->x_buf[i];
        f = c;
        outlet_float(x->x_obj.ob_outlet, f);
    }
}

static void sprint_bang(t_sprint *x)
{ // bang redoes the output
    sprint_drip(x);
}

static void sprint_float(t_sprint *x, t_float f)
{ // single floats
    x->x_len = sprintf(x->x_buf, "%g", f);
    sprint_drip(x);
}

static void sprint_list(t_sprint *x, t_symbol *s, int argc, t_atom *argv)
{ // lists with float as first element
    int i;
    //post ("selector %s", s->s_name);
    if (strncmp(s->s_name, "list", 5)) x->x_len = sprintf(x->x_buf, "%s", s->s_name);
    else x->x_len = 0;
    for (i = 0; i < argc; ++i)
    {
        if ((x->x_len != 0)&&(x->x_separator != -1)) x->x_len += sprintf(x->x_buf+x->x_len, "%c", x->x_separator);
        if (argv[i].a_type == A_SYMBOL)
        {
            x->x_len += sprintf(x->x_buf+x->x_len, "%s", argv[i].a_w.w_symbol->s_name);
        }
        else if (argv[i].a_type == A_FLOAT)
        {
            x->x_len += sprintf(x->x_buf+x->x_len, "%g", argv[i].a_w.w_float);
        }
    }
    sprint_drip(x);
}

static void sprint_anything(t_sprint *x, t_symbol *s, int argc, t_atom *argv)
{ // here we get symbols and lists of symbols
    int i;
    //post ("selector %s", s->s_name);
    x->x_len = sprintf(x->x_buf, "%s", s->s_name);

    for (i = 0; i < argc; ++i)
    {
        if ((x->x_len != 0)&&(x->x_separator != -1)) x->x_len += sprintf(x->x_buf+x->x_len, "%c", x->x_separator);
        if (argv[i].a_type == A_SYMBOL)
        {
            x->x_len += sprintf(x->x_buf+x->x_len, "%s", argv[i].a_w.w_symbol->s_name);
        }
        else if (argv[i].a_type == A_FLOAT)
        {
            x->x_len += sprintf(x->x_buf+x->x_len, "%g", argv[i].a_w.w_float);
        }
    }
    sprint_drip(x);
}

void sprint_setup(void)
{
    sprint_class = class_new(gensym("sprint"), (t_newmethod)sprint_new, 0,
        sizeof(t_sprint), 0, A_GIMME, 0);
    class_addbang(sprint_class, sprint_bang);
    class_addfloat(sprint_class, sprint_float);
    class_addlist(sprint_class, sprint_list);
    class_addanything(sprint_class, sprint_anything);
}

// fin sprint.c
