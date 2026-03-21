#include "jir_gen.h"
#include "generator.h"

void jir_generate_c(JirModule* mod, ASTNode* root, const char* out_path, bool is_main,
                    const char* mod_name, const char* mod_id) {
    (void)mod;
    (void)mod_name;
    (void)mod_id;
    generate_c_code(root, out_path, is_main);
}
