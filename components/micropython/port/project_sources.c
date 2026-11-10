#include <string.h>
#include "micropython_project.h"
#include "py/compile.h"
#include "py/builtin.h"
#include "py/reader.h"
#include "py/runtime.h"
#include "py/objstr.h"
#include "py/objmodule.h"
#include "py/objlist.h"
#include "py/mperrno.h"

static qstr project_name(void) { return qstr_from_str("lunar_project"); }
static mp_obj_t files(void) {
    mp_map_elem_t *module=mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
        MP_OBJ_NEW_QSTR(project_name()), MP_MAP_LOOKUP);
    if (!module) return MP_OBJ_NULL;
    mp_map_elem_t *value=mp_map_lookup(&mp_obj_module_get_globals(module->value)->map,
        MP_OBJ_NEW_QSTR(qstr_from_str("_files")), MP_MAP_LOOKUP);
    return value && mp_obj_is_type(value->value,&mp_type_dict) ? value->value : MP_OBJ_NULL;
}
static const char *relative_path(const char *path) {
    return path[0]=='.' && path[1]=='/' ? path+2 : path;
}
const char *micropython_project_source(const char *path,size_t *length) {
    mp_obj_t dictionary=files();
    if (dictionary==MP_OBJ_NULL) return NULL;
    mp_map_elem_t *item=mp_map_lookup(mp_obj_dict_get_map(dictionary),
        mp_obj_new_str(relative_path(path),strlen(relative_path(path))),MP_MAP_LOOKUP);
    return item ? mp_obj_str_get_data(item->value,length) : NULL;
}
int micropython_project_stat(const char *path) {
    size_t ignored=0;
    if (micropython_project_source(path,&ignored)) return MP_IMPORT_STAT_FILE;
    mp_obj_t dictionary=files(); if (dictionary==MP_OBJ_NULL) return MP_IMPORT_STAT_NO_EXIST;
    const char *prefix=relative_path(path);size_t length=strlen(prefix);
    while(length && prefix[length-1]=='/')--length;
    mp_map_t *map=mp_obj_dict_get_map(dictionary);
    for(size_t i=0;i<map->alloc;++i)if(mp_map_slot_is_filled(map,i)) {
        const char *key=mp_obj_str_get_str(map->table[i].key);
        if (!strncmp(key,prefix,length) && key[length]=='/') return MP_IMPORT_STAT_DIR;
    }
    return MP_IMPORT_STAT_NO_EXIST;
}
#ifndef LUNAROS_BROWSER_PROJECT_READER
mp_import_stat_t mp_import_stat(const char *path) { return micropython_project_stat(path); }
void mp_reader_new_file(mp_reader_t *reader,qstr filename) {
    size_t size=0;const char *source=micropython_project_source(qstr_str(filename),&size);
    if(!source)mp_raise_OSError(MP_ENOENT);
    mp_reader_new_mem(reader,(const byte *)source,size,0);
}
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    mp_reader_t reader;mp_reader_new_file(&reader,filename);
    return mp_lexer_new(filename,reader);
}
#endif
static bool safe_path(const char *path,size_t length) {
    if(!length || length>160 || path[0]=='/' || path[length-1]=='/')return false;
    size_t segment=0;
    for(size_t i=0;i<=length;++i) {
        unsigned char ch=path[i];
        if(i==length || ch=='/') {
            if(i==segment || (i-segment==1&&path[segment]=='.') ||
                (i-segment==2&&path[segment]=='.'&&path[segment+1]=='.'))return false;
            segment=i+1;
        } else if(ch==0 || ch=='\\' || ch==':' || ch<32)return false;
    }
    return true;
}
static bool reserved_path(const char *path) {
    const char *end=strchr(path,'/');size_t length=end?(size_t)(end-path):strlen(path);
    if(length>3&&!strcmp(path+length-3,".py"))length-=3;
    const char *names[]={"sys","builtins","machine","time","lunaredu","lunar","lunaros","lunaros_app_v1","lunar_project"};
    for(size_t i=0;i<sizeof(names)/sizeof(*names);++i)
        if(strlen(names[i])==length&&!strncmp(names[i],path,length))return true;
    return false;
}
static mp_obj_t run(mp_obj_t dictionary,mp_obj_t entry) {
    if(files()!=MP_OBJ_NULL)mp_raise_ValueError(MP_ERROR_TEXT("a project is already mounted"));
    if(!mp_obj_is_type(dictionary,&mp_type_dict))mp_raise_TypeError(MP_ERROR_TEXT("project files must be a dictionary"));
    mp_map_t *map=mp_obj_dict_get_map(dictionary);size_t bytes=0;
    if(!map->used || map->used>24)mp_raise_ValueError(MP_ERROR_TEXT("project needs 1..24 files"));
    for(size_t i=0;i<map->alloc;++i)if(mp_map_slot_is_filled(map,i)) {
        size_t size=0;const char *path=mp_obj_str_get_data(map->table[i].key,&size);
        if(!safe_path(path,size)||reserved_path(path))mp_raise_ValueError(MP_ERROR_TEXT("invalid or reserved project path"));
        const char *source=mp_obj_str_get_data(map->table[i].value,&size);bytes+=size;
        if(bytes>16384||memchr(source,0,size))mp_raise_ValueError(MP_ERROR_TEXT("project sources exceed capacity"));
    }
    const char *filename=mp_obj_str_get_str(entry);size_t entry_size=strlen(filename);
    if(!safe_path(filename,entry_size)||entry_size<4||strcmp(filename+entry_size-3,".py"))mp_raise_ValueError(MP_ERROR_TEXT("invalid project entry"));
    mp_obj_t module=mp_obj_new_module(project_name());
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(module)),
        MP_OBJ_NEW_QSTR(qstr_from_str("_files")),dictionary);
    size_t length=0;const char *source=micropython_project_source(filename,&length);
    if(!source)mp_raise_ValueError(MP_ERROR_TEXT("project entry missing"));
    // Read-only modules are rooted in lunar_project for the full VM session.
    // Native import handles packages, relative imports and circular imports.
    mp_obj_t paths=mp_obj_new_list(0,NULL);
    const char *slash=strrchr(filename,'/');
    if(slash)mp_obj_list_append(paths,mp_obj_new_str(filename,slash-filename));
    mp_obj_list_append(paths,MP_OBJ_NEW_QSTR(MP_QSTR_));
    mp_sys_path=paths;
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_globals_get()),MP_OBJ_NEW_QSTR(qstr_from_str("__file__")),entry);
    mp_lexer_t *lexer=mp_lexer_new_from_str_len(qstr_from_str(filename),source,length,0);
    mp_parse_tree_t tree=mp_parse(lexer,MP_PARSE_FILE_INPUT);
    mp_obj_t function=mp_compile(&tree,qstr_from_str(filename),false);
    mp_call_function_0(function);return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(run_obj,run);
static mp_obj_t read_text(mp_obj_t name) {
    size_t length=0;const char *source=micropython_project_source(mp_obj_str_get_str(name),&length);
    if(!source)mp_raise_OSError(MP_ENOENT);
    return mp_obj_new_str(source,length);
}
static MP_DEFINE_CONST_FUN_OBJ_1(read_text_obj,read_text);
void micropython_project_init(void) {
    mp_obj_t module=mp_obj_new_module(project_name());mp_obj_t globals=MP_OBJ_FROM_PTR(mp_obj_module_get_globals(module));
    mp_obj_dict_store(globals,MP_OBJ_NEW_QSTR(qstr_from_str("run")),MP_OBJ_FROM_PTR(&run_obj));
    mp_obj_dict_store(globals,MP_OBJ_NEW_QSTR(qstr_from_str("read_text")),MP_OBJ_FROM_PTR(&read_text_obj));
}
