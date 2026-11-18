# genmarshal.awk - write GTK+'s signal marshallers
#
# Usage: awk -v out_h=gtkmarshal.h -v out_c=gtkmarshal.c \
#            -f genmarshal.awk gtkmarshal.list
#
# Each line of the list is RETURN:PARAM,PARAM,... in GTK's fundamental
# type names. For each distinct signature this writes a marshaller,
# which calls a signal handler with the arguments from a GtkArg array,
# to out_c, and its prototype to out_h. Signatures that differ only in
# types passed the same way (pointers of any kind, enums and ints) share
# one marshaller, through a #define.
#
# POSIX awk; this replaces genmarshal.pl, whose output it reproduces.

BEGIN {
  trans["NONE"] = "void";       trans["CHAR"] = "char";
  trans["BOOL"] = "gboolean";   trans["INT"] = "gint";
  trans["UINT"] = "guint";      trans["LONG"] = "glong";
  trans["ULONG"] = "gulong";    trans["FLOAT"] = "gfloat";
  trans["DOUBLE"] = "gdouble";  trans["STRING"] = "gpointer";
  trans["ENUM"] = "gint";       trans["FLAGS"] = "gint";
  trans["BOXED"] = "gpointer";  trans["POINTER"] = "gpointer";
  trans["OBJECT"] = "gpointer"; trans["FOREIGN"] = "FOREIGN";
  trans["C_CALLBACK"] = "C_CALLBACK";
  trans["SIGNAL"] = "SIGNAL";   trans["ARGS"] = "ARGS";
  trans["CALLBACK"] = "CALLBACK";

  if (out_h == "" || out_c == "") {
    print "genmarshal.awk: set out_h and out_c with -v" > "/dev/stderr";
    failed = 1;
    exit 1;
  }

  printf "#ifndef __GTK_MARSHAL_H__\n" \
         "#define __GTK_MARSHAL_H__\n" \
         "\n" \
         "#include <gtk/gtktypeutils.h>\n" \
         "#include <gtk/gtkobject.h>\n" \
         "\n" \
         "#ifdef __cplusplus\n" \
         "extern \"C\" {\n" \
         "#endif /* __cplusplus */\n" \
         "\n" \
         "#define gtk_signal_default_marshaller gtk_marshal_NONE__NONE\n" \
         "\n" > out_h;
  printf "#include \"gtkmarshal.h\"\n\n" > out_c;
}

function lookup(t)
{
  return (t in trans) ? trans[t] : "";
}

# The marshaller's name for the signature in retval and p[1..np].
function signature(    s, i)
{
  s = retval "__";
  for (i = 1; i <= np; i++)
    s = s (i > 1 ? "_" : "") p[i];
  return s;
}

# The type a value of type t is passed as, for sharing marshallers.
function passed_as(t)
{
  if (lookup(t) == "gpointer")
    t = "POINTER";
  if (t == "ENUM")
    t = "UINT";
  if (t == "UINT")
    t = "INT";
  if (t == "ULONG")
    t = "LONG";
  return t;
}

function shares(t)
{
  return (lookup(t) == "gpointer" && t != "POINTER") || \
         t == "ENUM" || t == "UINT" || t == "ULONG";
}

{
  i = index($0, ":");
  if (i) {
    retval = substr($0, 1, i - 1);
    params = substr($0, i + 1);
  } else {
    retval = $0;
    params = "";
  }
  np = split(params, p, /[ \t]*,[ \t]*/);
  while (np > 0 && p[np] == "")
    np--;

  funcname = signature();
  if (funcname in defined)
    next;

  equiv = shares(retval);
  for (i = 1; i <= np && !equiv; i++)
    equiv = shares(p[i]);

  if (equiv) {
    printf "#define gtk_marshal_%s ", funcname > out_h;
    defined[funcname] = 1;
    for (i = 1; i <= np; i++)
      p[i] = passed_as(p[i]);
    retval = passed_as(retval);
    funcname = signature();
    printf "gtk_marshal_%s\n\n", funcname > out_h;
    if (funcname in defined)
      next;
  }
  defined[funcname] = 1;

  printf "void gtk_marshal_%s (GtkObject    *object, \n" \
         "                            GtkSignalFunc func, \n" \
         "                            gpointer      func_data, \n" \
         "                            GtkArg       *args);\n" \
         "\n", funcname > out_h;

  printf "typedef %s (*GtkSignal_%s) (GtkObject *object, \n",
         lookup(retval), funcname > out_c;
  argn = 1;
  for (i = 1; i <= np; i++) {
    if (p[i] == "C_CALLBACK") {
      printf "GtkFunction arg%da,\ngpointer    arg%db,\n", argn, argn > out_c;
      argn++;
    } else if (p[i] == "SIGNAL" || p[i] == "ARGS" || p[i] == "FOREIGN") {
      printf "gpointer arg%da,\ngpointer arg%db,\n", argn, argn > out_c;
      argn++;
    } else if (p[i] == "CALLBACK") {
      printf "gpointer arg%da,\ngpointer arg%db,\ngpointer arg%dc,\n",
             argn, argn, argn > out_c;
      argn++;
    } else if (p[i] != "NONE") {
      printf "%s arg%d,\n", lookup(p[i]), argn > out_c;
      argn++;
    }
  }
  printf "gpointer user_data);\n" > out_c;

  printf "void gtk_marshal_%s (GtkObject    *object, \n" \
         "                            GtkSignalFunc func, \n" \
         "                            gpointer      func_data, \n" \
         "                            GtkArg       *args)\n" \
         "{\n" \
         "  GtkSignal_%s rfunc;\n", funcname, funcname > out_c;
  if (retval != "NONE") {
    printf "  %s  *return_val;\n", lookup(retval) > out_c;
    retn = (p[1] == "NONE") ? 0 : np;
    printf "  return_val = GTK_RETLOC_%s (args[%d]);\n", retval, retn > out_c;
  }
  printf "  rfunc = (GtkSignal_%s) func;\n", funcname > out_c;
  if (retval != "NONE")
    printf "  *return_val = " > out_c;
  printf " (* rfunc) (object,\n" > out_c;
  for (i = 1; i <= np; i++) {
    a = i - 1;
    if (p[i] == "C_CALLBACK")
      printf "GTK_VALUE_C_CALLBACK(args[%d]).func,\n" \
             "GTK_VALUE_C_CALLBACK(args[%d]).func_data,\n", a, a > out_c;
    else if (p[i] == "SIGNAL")
      printf "GTK_VALUE_SIGNAL(args[%d]).f,\n" \
             "GTK_VALUE_SIGNAL(args[%d]).d,\n", a, a > out_c;
    else if (p[i] == "CALLBACK")
      printf "GTK_VALUE_CALLBACK(args[%d]).marshal,\n" \
             "GTK_VALUE_CALLBACK(args[%d]).data,\n" \
             "GTK_VALUE_CALLBACK(args[%d]).notify,\n", a, a, a > out_c;
    else if (p[i] == "FOREIGN")
      printf "GTK_VALUE_FOREIGN(args[%d]).data,\n" \
             "GTK_VALUE_FOREIGN(args[%d]).notify,\n", a, a > out_c;
    else if (p[i] != "NONE")
      printf "                      GTK_VALUE_%s(args[%d]),\n", p[i], a > out_c;
  }
  printf "                             func_data);\n}\n\n" > out_c;
}

END {
  if (failed)
    exit 1;
  printf "\n" \
         "#ifdef __cplusplus\n" \
         "}\n" \
         "#endif /* __cplusplus */\n" \
         "\n" \
         "#endif /* __GTK_MARSHAL_H__ */\n" > out_h;
  close(out_h);
  close(out_c);
}
