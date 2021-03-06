#include <mruby.h>
#include <mruby/string.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <stdio.h>
#include <math.h>
#include "parson.h"

#if 1
#define ARENA_SAVE \
  int ai = mrb_gc_arena_save(mrb); \
  if (ai == MRB_ARENA_SIZE) { \
    mrb_raise(mrb, E_RUNTIME_ERROR, "arena overflow"); \
  }
#define ARENA_RESTORE \
  mrb_gc_arena_restore(mrb, ai);
#else
#define ARENA_SAVE
#define ARENA_RESTORE
#endif

/*********************************************************
 * main
 *********************************************************/
static mrb_value
mrb_value_to_string(mrb_state* mrb, mrb_value value) {
  mrb_value str;

  if (mrb_nil_p(value)) {
    return mrb_str_new_cstr(mrb, "null");
  }

  switch (mrb_type(value)) {
  case MRB_TT_FIXNUM:
  case MRB_TT_FLOAT:
  case MRB_TT_TRUE:
  case MRB_TT_FALSE:
  case MRB_TT_UNDEF:
    str = mrb_funcall(mrb, value, "to_s", 0, NULL);
    break;
  case MRB_TT_SYMBOL:
    value = mrb_funcall(mrb, value, "to_s", 0, NULL);
    /* FALLTHROUGH */
  case MRB_TT_STRING:
    {
      int ai = mrb_gc_arena_save(mrb);
      char* ptr = RSTRING_PTR(value);
      char* end = RSTRING_END(value);
      str = mrb_str_new_cstr(mrb, "\""); 
      while (ptr < end && *ptr) {
        switch (*ptr) {
        case '\\':
          str = mrb_str_cat_cstr(mrb, str, "\\\\");
          break;
        case '"':
          str = mrb_str_cat_cstr(mrb, str, "\\\"");
          break;
        case '\b':
          str = mrb_str_cat_cstr(mrb, str, "\\b");
          break;
        case '\f':
          str = mrb_str_cat_cstr(mrb, str, "\\f");
          break;
        case '\n':
          str = mrb_str_cat_cstr(mrb, str, "\\n");
          break;
        case '\r':
          str = mrb_str_cat_cstr(mrb, str, "\\r");
          break;
        case '\t':
          str = mrb_str_cat_cstr(mrb, str, "\\t");
          break;
        default:
          // TODO: handle unicode
          str = mrb_str_cat(mrb, str, ptr, 1);
        }
        ptr++;
      }
      mrb_str_cat_cstr(mrb, str, "\""); 
      mrb_gc_arena_restore(mrb, ai);
    }
    break;
  case MRB_TT_HASH:
    {
      mrb_value keys;
      int n, l;
      str = mrb_str_new_cstr(mrb, "{");
      keys = mrb_hash_keys(mrb, value);
      l = RARRAY_LEN(keys);
      for (n = 0; n < l; n++) {
        mrb_value obj;
        int ai = mrb_gc_arena_save(mrb);
        mrb_value key = mrb_ary_entry(keys, n);
        mrb_value enckey = mrb_funcall(mrb, key, "to_s", 0, NULL);
        enckey = mrb_funcall(mrb, enckey, "inspect", 0, NULL);
        mrb_str_concat(mrb, str, enckey);
        mrb_str_cat_cstr(mrb, str, ":");
        obj = mrb_hash_get(mrb, value, key);
        mrb_str_concat(mrb, str, mrb_value_to_string(mrb, obj));
        if (n != l - 1) {
          mrb_str_cat_cstr(mrb, str, ",");
        }
        mrb_gc_arena_restore(mrb, ai);
      }
      mrb_str_cat_cstr(mrb, str, "}");
      break;
    }
  case MRB_TT_ARRAY:
    {
      int n, l;
      str = mrb_str_new_cstr(mrb, "[");
      l = RARRAY_LEN(value);
      for (n = 0; n < l; n++) {
        int ai = mrb_gc_arena_save(mrb);
        mrb_value obj = mrb_ary_entry(value, n);
        mrb_str_concat(mrb, str, mrb_value_to_string(mrb, obj));
        if (n != l - 1) {
          mrb_str_cat_cstr(mrb, str, ",");
        }
        mrb_gc_arena_restore(mrb, ai);
      }
      mrb_str_cat_cstr(mrb, str, "]");
      break;
    }
  default:
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  return str;
}

static mrb_value
json_value_to_mrb_value(mrb_state* mrb, JSON_Value* value) {
  mrb_value ret;
  switch (json_value_get_type(value)) {
  case JSONError:
  case JSONNull:
    ret = mrb_nil_value();
    break;
  case JSONString:
    ret = mrb_str_new_cstr(mrb, json_value_get_string(value));
    break;
  case JSONNumber:
    {
      double d = json_value_get_number(value);
      if (floor(d) == d) {
        ret = mrb_fixnum_value(d);
      }
      else {
        ret = mrb_float_value(mrb, d);
      }
    }
    break;
  case JSONObject:
    {
      mrb_value hash = mrb_hash_new(mrb);
      JSON_Object* object = json_value_get_object(value);
      size_t count = json_object_get_count(object);
      size_t n;
      for (n = 0; n < count; n++) {
        int ai = mrb_gc_arena_save(mrb);
        const char* name = json_object_get_name(object, n);
        mrb_hash_set(mrb, hash, mrb_str_new_cstr(mrb, name),
          json_value_to_mrb_value(mrb, json_object_get_value(object, name)));
        mrb_gc_arena_restore(mrb, ai);
      }
      ret = hash;
    }
    break;
  case JSONArray:
    {
      mrb_value ary;
      JSON_Array* array;
      size_t n, count;
      ary = mrb_ary_new(mrb);
      array = json_value_get_array(value);
      count = json_array_get_count(array);
      for (n = 0; n < count; n++) {
        int ai = mrb_gc_arena_save(mrb);
        JSON_Value* elem = json_array_get_value(array, n);
        mrb_ary_push(mrb, ary, json_value_to_mrb_value(mrb, elem));
        mrb_gc_arena_restore(mrb, ai);
      }
      ret = ary;
    }
    break;
  case JSONBoolean:
    if (json_value_get_boolean(value))
      ret = mrb_true_value();
    else
      ret = mrb_false_value();
    break;
  default:
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid argument");
  }
  return ret;
}

static mrb_value
mrb_json_parse(mrb_state *mrb, mrb_value self)
{
  mrb_value value;
  JSON_Value *root_value;
  mrb_value json = mrb_nil_value();
  mrb_get_args(mrb, "S", &json);

  root_value = json_parse_string(mrb_str_to_cstr(mrb, json));
  if (!root_value) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid json");
  }

  value = json_value_to_mrb_value(mrb, root_value);
  json_value_free(root_value);
  return value;
}

static mrb_value
mrb_json_stringify(mrb_state *mrb, mrb_value self)
{
  mrb_value obj;
  mrb_get_args(mrb, "o", &obj);
  return mrb_value_to_string(mrb, obj);
}

/*********************************************************
 * register
 *********************************************************/

void
mrb_mruby_json_gem_init(mrb_state* mrb) {
  struct RClass *_class_json = mrb_define_module(mrb, "JSON");
  mrb_define_class_method(mrb, _class_json, "parse", mrb_json_parse, MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, _class_json, "stringify", mrb_json_stringify, MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, _class_json, "generate", mrb_json_stringify, MRB_ARGS_REQ(1));
}

void
mrb_mruby_json_gem_final(mrb_state* mrb) {
}

/* vim:set et ts=2 sts=2 sw=2 tw=0: */
