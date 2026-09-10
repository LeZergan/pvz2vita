/* Included by glutil.c after shader source tracking. Render-thread only.
 * Keep native compiled pairs alive with an unlinked holder program. VitaGL's
 * postponed linker can reuse their GXP while still building each new program's
 * own attribute bindings and uniform storage. Never deserialize disk binaries.
 * Pair keys use exact patched sources, since varying semantics are paired.
 */
#define SHADER_PAIR_CAP 16u
#define SHADER_PAIR_SOURCE_BUDGET (256u * 1024u)
#define SHADER_PAIR_BINARY_BUDGET (1024u * 1024u)
#ifndef GL_PROGRAM_BINARY_LENGTH
#define GL_PROGRAM_BINARY_LENGTH 0x8741
#endif
extern void glGetAttachedShaders(GLuint, GLsizei, GLsizei *, GLuint *);
typedef struct {
    GLuint holder, shaders[2];
    char *source;
    size_t lengths[2];
    int reusable;
} shader_pair_entry;
static shader_pair_entry shader_pairs[SHADER_PAIR_CAP];
static unsigned shader_pair_count, shader_pair_source_bytes, shader_pair_binary_bytes;

static void shader_pairs_source_changed(GLuint shader) {
    for (unsigned i = 0; i < shader_pair_count; ++i)
        if (shader_pairs[i].shaders[0] == shader || shader_pairs[i].shaders[1] == shader)
            shader_pairs[i].reusable = 0;
    /* No eviction: native DeleteProgram waits for the GPU. Retention stays
     * bounded even if the game mutates a cached shader; never reuse that pair. */
}

static void shader_pairs_link(GLuint program) {
    GLuint shaders[2];
    GLsizei count = 0;
    GLint linked = GL_FALSE;
    shader_diag_entry *sources[2] = {NULL, NULL};
    if (!program || program > PVZ2_VGL_PROGRAM_LIMIT || !glIsProgram(program)) {
        glLinkProgram(program);
        return;
    }
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked) { glLinkProgram(program); return; }
    glGetAttachedShaders(program, 2, &count, shaders);
    if (count == 2) {
        /* The pinned VitaGL returns vertex, fragment in this order. Check it
         * explicitly so unknown shader configurations take the normal path. */
        GLint vertex = 0, fragment = 0;
        glGetShaderiv(shaders[0], GL_SHADER_TYPE, &vertex);
        glGetShaderiv(shaders[1], GL_SHADER_TYPE, &fragment);
        if (vertex == GL_VERTEX_SHADER && fragment == GL_FRAGMENT_SHADER) {
            sources[0] = get_shader_diag_entry(shaders[0], 0);
            sources[1] = get_shader_diag_entry(shaders[1], 0);
        }
    }
    if (!sources[0] || !sources[1] || !sources[0]->owned_source ||
        !sources[1]->owned_source || !sources[0]->owned_source_len ||
        !sources[1]->owned_source_len) {
        ++pvz2_pair_bypasses;
        glLinkProgram(program);
        return;
    }
    size_t vertex_len = sources[0]->owned_source_len, fragment_len = sources[1]->owned_source_len;
    for (unsigned i = 0; i < shader_pair_count; ++i) {
        shader_pair_entry *e = &shader_pairs[i];
        if (e->reusable && e->lengths[0] == vertex_len && e->lengths[1] == fragment_len &&
            !memcmp(e->source, sources[0]->owned_source, vertex_len) &&
            !memcmp(e->source + vertex_len, sources[1]->owned_source, fragment_len)) {
            glAttachShader(program, e->shaders[0]);
            glAttachShader(program, e->shaders[1]);
            glLinkProgram(program);
            ++pvz2_pair_hits;
            return;
        }
    }
    ++pvz2_pair_misses;
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked || shader_pair_count == SHADER_PAIR_CAP ||
        vertex_len > SHADER_PAIR_SOURCE_BUDGET || fragment_len > SHADER_PAIR_SOURCE_BUDGET ||
        vertex_len + fragment_len > SHADER_PAIR_SOURCE_BUDGET - shader_pair_source_bytes) return;
    GLint binary_bytes = 0;
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binary_bytes);
    if (binary_bytes <= 0 || (unsigned)binary_bytes > SHADER_PAIR_BINARY_BUDGET - shader_pair_binary_bytes) return;
    char *copy = malloc(vertex_len + fragment_len);
    if (!copy) return;
    GLuint holder = glCreateProgram();
    if (!holder || holder > PVZ2_VGL_PROGRAM_LIMIT) { free(copy); return; }
    memcpy(copy, sources[0]->owned_source, vertex_len);
    memcpy(copy + vertex_len, sources[1]->owned_source, fragment_len);
    glAttachShader(holder, shaders[0]);
    glAttachShader(holder, shaders[1]);
    shader_pairs[shader_pair_count++] = (shader_pair_entry){
        holder, {shaders[0], shaders[1]}, copy, {vertex_len, fragment_len}, 1
    };
    shader_pair_source_bytes += vertex_len + fragment_len;
    shader_pair_binary_bytes += binary_bytes;
}
