// Copyright (c) Caden Ji. All rights reserved.
//
// Licensed under the MIT License. See LICENSE file in the project root for
// license information.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "graphics/framebuffer.h"
#include "graphics/rasterizer.h"
#include "graphics/texture.h"
#include "math/math_utility.h"
#include "math/matrix.h"
#include "math/vector.h"
#include "shaders/shadow_casting.h"
#include "shaders/standard.h"
#include "utilities/image.h"
#include "utilities/mesh.h"

#define SHADOW_MAP_WIDTH 1024
#define SHADOW_MAP_HEIGHT 1024
#define IMAGE_WIDTH 1536
#define IMAGE_HEIGHT 1024
#define FPS 30
#define ANIMATION_TIME 4.5f

struct model {
    struct mesh *mesh;
    struct texture *base_color_map;
    struct texture *normal_map;
    struct texture *metallic_map;
    struct texture *roughness_map;
};

static vector3 light_direction = (vector3){{0.0f, 0.24f, -0.326f}};
static vector3 camera_position = (vector3){{0.0f, 0.24f, 0.326f}};
static vector3 camera_target = (vector3){{0.0f, 0.0f, 0.0f}};
static float rotation_y = 0.796f;
static float fov = PI / 3.2f;

static struct framebuffer *shadow_framebuffer;
static struct texture *shadow_map;
static struct framebuffer *framebuffer;
static struct texture *color_buffer;
static struct texture *depth_buffer;

static matrix4x4 light_world2clip;

static void initialize_rendering(void) {
    shadow_framebuffer = create_framebuffer();
    shadow_map = create_texture(TEXTURE_FORMAT_DEPTH_FLOAT, 1, 1);
    float shadow_value = 1.0f;
    set_texture_pixels(shadow_map, &shadow_value);
    attach_texture_to_framebuffer(shadow_framebuffer, DEPTH_ATTACHMENT,
                                  shadow_map);

    framebuffer = create_framebuffer();
    color_buffer =
        create_texture(TEXTURE_FORMAT_SRGB8_A8, IMAGE_WIDTH, IMAGE_HEIGHT);
    depth_buffer =
        create_texture(TEXTURE_FORMAT_DEPTH_FLOAT, IMAGE_WIDTH, IMAGE_HEIGHT);
    attach_texture_to_framebuffer(framebuffer, COLOR_ATTACHMENT, color_buffer);
    attach_texture_to_framebuffer(framebuffer, DEPTH_ATTACHMENT, depth_buffer);
}

static void end_rendering(void) {
    destroy_texture(shadow_map);
    destroy_texture(color_buffer);
    destroy_texture(depth_buffer);
    destroy_framebuffer(shadow_framebuffer);
    destroy_framebuffer(framebuffer);
}

static void render_model(const struct model *model) {
    set_viewport(0, 0, IMAGE_WIDTH, IMAGE_HEIGHT);
    set_vertex_shader(standard_vertex_shader);
    set_fragment_shader(standard_fragment_shader);
    set_clear_color(0.0f, 0.0f, 0.0f, 0.0f);
    clear_framebuffer(framebuffer);

    struct standard_uniform uniform;
    uniform.local2world = matrix4x4_rotate_y(rotation_y);
    matrix4x4 world2view = matrix4x4_look_at(camera_position, camera_target,
                                             (vector3){{0.0f, 1.0f, 0.0f}});
    matrix4x4 view2clip = matrix4x4_perspective(
        fov, (float)IMAGE_WIDTH / IMAGE_HEIGHT, 0.1f, 5.0f);
    uniform.world2clip = matrix4x4_multiply(view2clip, world2view);
    uniform.local2world_direction = matrix4x4_to_3x3(uniform.local2world);
    // There is no non-uniform scaling so the normal transformation matrix is
    // the direction transformation matrix.
    uniform.local2world_normal = uniform.local2world_direction;
    uniform.camera_position = camera_position;
    uniform.light_direction = vector3_normalize(light_direction);
    uniform.illuminance = (vector3){{1.0f, 1.0f, 1.0f}};
    // Remap each component of position from [-1, 1] to [0, 1].
    matrix4x4 scale_bias = {{{0.5f, 0.0f, 0.0f, 0.5f},
                             {0.0f, 0.5f, 0.0f, 0.5f},
                             {0.0f, 0.0f, 0.5f, 0.5f},
                             {0.0f, 0.0f, 0.0f, 1.0f}}};
    uniform.world2light = matrix4x4_multiply(scale_bias, light_world2clip);
    uniform.shadow_map = shadow_map;
    uniform.ambient_luminance = (vector3){{2.0f, 1.2f, 0.9f}};
    uniform.normal_map = model->normal_map;
    uniform.base_color = VECTOR3_ONE;
    uniform.base_color_map = model->base_color_map;
    uniform.metallic = 1.0f;
    uniform.metallic_map = model->metallic_map;
    uniform.roughness = 1.0f;
    uniform.roughness_map = model->roughness_map;
    uniform.reflectance = 0.5f;  // Common dielectric surfaces F0.

    const struct mesh *mesh = model->mesh;
    uint32_t triangle_count = mesh->triangle_count;
    for (size_t t = 0; t < triangle_count; t++) {
        struct standard_vertex_attribute attributes[3];
        const void *attribute_ptrs[3];
        for (uint32_t v = 0; v < 3; v++) {
            get_mesh_position(&attributes[v].position, mesh, t, v);
            get_mesh_normal(&attributes[v].normal, mesh, t, v);
            get_mesh_tangent(&attributes[v].tangent, mesh, t, v);
            get_mesh_texcoord(&attributes[v].texcoord, mesh, t, v);
            attribute_ptrs[v] = attributes + v;
        }
        draw_triangle(framebuffer, &uniform, attribute_ptrs);
    }
}

int main(void) {
    const char *model_path = "assets/violin/violin.obj";
    const char *base_color_map_path = "assets/violin/base_color.tga";
    const char *normal_map_path = "assets/violin/normal.tga";
    const char *metallic_map_path = "assets/violin/metallic.tga";
    const char *roughness_map_path = "assets/violin/roughness.tga";

    struct model model;
    model.mesh = load_mesh(model_path);
    if (model.mesh == NULL) {
        printf("Cannot load .obj file.\n");
        return 0;
    }
    model.base_color_map = load_image(base_color_map_path, true);
    model.normal_map = load_image(normal_map_path, false);
    model.metallic_map = load_image(metallic_map_path, false);
    model.roughness_map = load_image(roughness_map_path, false);
    if (model.base_color_map == NULL || model.normal_map == NULL ||
        model.metallic_map == NULL || model.roughness_map == NULL) {
        printf("Cannot load texture files.\n");
        destroy_mesh(model.mesh);
        destroy_texture(model.base_color_map);
        destroy_texture(model.normal_map);
        destroy_texture(model.metallic_map);
        destroy_texture(model.roughness_map);
        return 0;
    }

    initialize_rendering();
    float light_direction_delta_x_start = 0.2f;
    float light_direction_delta_x_end = -0.2f;
    vector3 light_direction_std = light_direction;
    float camera_distance_start = 0.4f;
    float camera_distance_end = 0.3f;
    vector3 camera_position_std = vector3_normalize(camera_position);
    int frame_count = (int)(ANIMATION_TIME * FPS);
    for (int i = 0; i < frame_count; i++) {
        float t = (float)i / frame_count;
        // Lerp camera position.
        float camera_distance =
            float_lerp(camera_distance_start, camera_distance_end, t);
        camera_position =
            vector3_multiply_scalar(camera_position_std, camera_distance);
        // Lerp light direction.
        float light_direction_delta_x = float_lerp(
            light_direction_delta_x_start, light_direction_delta_x_end, t);
        light_direction =
            vector3_add(light_direction_std,
                        (vector3){{light_direction_delta_x, 0.0f, 0.0f}});
        render_model(&model);
        char image_name[30];
        sprintf(image_name, "violin/v-%.3d.tga", i);
        save_image(color_buffer, image_name, true);
    }
    end_rendering();

    destroy_mesh(model.mesh);
    destroy_texture(model.base_color_map);
    destroy_texture(model.normal_map);
    destroy_texture(model.metallic_map);
    destroy_texture(model.roughness_map);
    return 0;
}
