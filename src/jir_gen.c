#include "jir_gen.h"
#include "generator.h"

void jir_generate_c(JirModule* mod, CodegenModuleInfo* info, const char* out_path, bool is_main,
                    const char* mod_name, const char* mod_id) {
    (void)mod_name;
    (void)mod_id;
    generate_c_code_from_jir(mod, info, out_path, is_main);
}
