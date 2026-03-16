#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glx.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#ifndef CALLBACK
#define CALLBACK
#endif

#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080
#define PI 3.14159265358979323846
#define GLOBE_RADIUS 1.55
#define LABEL_RADIUS 1.67
#define GRID_RADIUS 1.56
#define CAMERA_DISTANCE 4.6
#define LATITUDE_STEP 5.0
#define LONGITUDE_STEP 5.0
#define ROTATION_SENSITIVITY 0.35f
#define EARTH_DAY_TEXTURE_PATH "assets/earth_daymap.ppm"
#define EARTH_NIGHT_TEXTURE_PATH "assets/earth_nightmap.ppm"

typedef struct {
    int width;
    int height;
    GLuint earth_texture;
    int earth_texture_width;
    int earth_texture_height;
    unsigned char *earth_day_pixels;
    unsigned char *earth_night_pixels;
    unsigned char *earth_lit_pixels;
    time_t last_daylight_update;
    float yaw_degrees;
    float pitch_degrees;
    bool dragging;
    int last_mouse_x;
    int last_mouse_y;
} AppState;

static const unsigned char FONT_DIGITS[10][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
    {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}
};

static const unsigned char FONT_PLUS[7] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
static const unsigned char FONT_MINUS[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
static const unsigned char FONT_COLON[7] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
static const unsigned char FONT_PERIOD[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x04};
static const unsigned char FONT_SPACE[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char FONT_U[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
static const unsigned char FONT_T[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
static const unsigned char FONT_C[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
static const unsigned char FONT_D[7] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
static const unsigned char FONT_E[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};

static const unsigned char *glyph_for_char(char c) {
    if (c >= '0' && c <= '9') {
        return FONT_DIGITS[c - '0'];
    }

    switch (c) {
        case '+':
            return FONT_PLUS;
        case '-':
            return FONT_MINUS;
        case ':':
            return FONT_COLON;
        case '.':
            return FONT_PERIOD;
        case ' ':
            return FONT_SPACE;
        case 'U':
            return FONT_U;
        case 'T':
            return FONT_T;
        case 'C':
            return FONT_C;
        case 'D':
            return FONT_D;
        case 'E':
            return FONT_E;
        default:
            return FONT_SPACE;
    }
}

static float clampf(float value, float min_value, float max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static double degrees_to_radians(double degrees) {
    return degrees * PI / 180.0;
}

static void geo_to_cartesian(double lon, double lat, double radius, double *x, double *y, double *z) {
    double lon_radians = degrees_to_radians(lon);
    double lat_radians = degrees_to_radians(lat);
    double cos_lat = cos(lat_radians);

    *x = radius * cos_lat * sin(lon_radians);
    *y = radius * sin(lat_radians);
    *z = radius * cos_lat * cos(lon_radians);
}

static void set_ortho_projection(int width, int height) {
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble) width, (GLdouble) height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void set_perspective_projection(const AppState *app) {
    double aspect = (app->height == 0) ? 1.0 : (double) app->width / (double) app->height;

    glViewport(0, 0, app->width, app->height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(42.0, aspect, 0.1, 50.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslated(0.0, 0.0, -CAMERA_DISTANCE);
    glRotated(18.0 + app->pitch_degrees, 1.0, 0.0, 0.0);
    glRotated(app->yaw_degrees, 0.0, 1.0, 0.0);
}

static void draw_rect(float x0, float y0, float x1, float y1) {
    glBegin(GL_QUADS);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
}

static void draw_outline_rect(float x0, float y0, float x1, float y1) {
    glBegin(GL_LINE_LOOP);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
}

static void draw_pixel_text(float x, float y, float scale, const char *text) {
    glBegin(GL_QUADS);
    for (size_t i = 0; text[i] != '\0'; ++i) {
        const unsigned char *glyph = glyph_for_char(text[i]);
        float gx = x + (float) i * scale * 6.0f;

        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (((glyph[row] >> (4 - col)) & 1U) == 0U) {
                    continue;
                }
                float px = gx + (float) col * scale;
                float py = y + (float) row * scale;
                glVertex2f(px, py);
                glVertex2f(px + scale, py);
                glVertex2f(px + scale, py + scale);
                glVertex2f(px, py + scale);
            }
        }
    }
    glEnd();
}

static void compute_surface_frame(
    double lon,
    double lat,
    double *center_x,
    double *center_y,
    double *center_z,
    double *east_x,
    double *east_y,
    double *east_z,
    double *north_x,
    double *north_y,
    double *north_z
) {
    double lon_radians = degrees_to_radians(lon);
    double lat_radians = degrees_to_radians(lat);

    geo_to_cartesian(lon, lat, 1.0, center_x, center_y, center_z);

    *east_x = cos(lon_radians);
    *east_y = 0.0;
    *east_z = -sin(lon_radians);

    *north_x = -sin(lat_radians) * sin(lon_radians);
    *north_y = cos(lat_radians);
    *north_z = -sin(lat_radians) * cos(lon_radians);
}

static void emit_textured_sphere_vertex(double lon, double lat, double radius);
static void emit_colored_sphere_vertex(double lon, double lat, double radius);

static void emit_surface_offset_vertex(
    double radius,
    double center_x,
    double center_y,
    double center_z,
    double east_x,
    double east_y,
    double east_z,
    double north_x,
    double north_y,
    double north_z,
    double offset_x,
    double offset_y
) {
    double x = center_x * radius + east_x * offset_x + north_x * offset_y;
    double y = center_y * radius + east_y * offset_x + north_y * offset_y;
    double z = center_z * radius + east_z * offset_x + north_z * offset_y;
    double scale = radius / sqrt(x * x + y * y + z * z);

    x *= scale;
    y *= scale;
    z *= scale;

    glNormal3d(x / radius, y / radius, z / radius);
    glVertex3d(x, y, z);
}

static void draw_surface_text(double lon, double lat, double radius, double pixel_size, const char *text) {
    double center_x;
    double center_y;
    double center_z;
    double east_x;
    double east_y;
    double east_z;
    double north_x;
    double north_y;
    double north_z;
    size_t length = 0;
    double total_width;
    double origin_x;
    double origin_y;

    while (text[length] != '\0') {
        ++length;
    }

    total_width = (double) length * 6.0 * pixel_size;
    origin_x = -total_width * 0.5;
    origin_y = 3.5 * pixel_size;

    compute_surface_frame(
        lon,
        lat,
        &center_x,
        &center_y,
        &center_z,
        &east_x,
        &east_y,
        &east_z,
        &north_x,
        &north_y,
        &north_z
    );

    glBegin(GL_QUADS);
    for (size_t i = 0; i < length; ++i) {
        const unsigned char *glyph = glyph_for_char(text[i]);
        double glyph_x = origin_x + (double) i * 6.0 * pixel_size;

        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (((glyph[row] >> (4 - col)) & 1U) == 0U) {
                    continue;
                }

                double x0 = glyph_x + (double) col * pixel_size;
                double x1 = x0 + pixel_size;
                double y1 = origin_y - (double) row * pixel_size;
                double y0 = y1 - pixel_size;

                emit_surface_offset_vertex(radius, center_x, center_y, center_z, east_x, east_y, east_z, north_x, north_y, north_z, x0, y0);
                emit_surface_offset_vertex(radius, center_x, center_y, center_z, east_x, east_y, east_z, north_x, north_y, north_z, x1, y0);
                emit_surface_offset_vertex(radius, center_x, center_y, center_z, east_x, east_y, east_z, north_x, north_y, north_z, x1, y1);
                emit_surface_offset_vertex(radius, center_x, center_y, center_z, east_x, east_y, east_z, north_x, north_y, north_z, x0, y1);
            }
        }
    }
    glEnd();
}

static bool load_ppm_texture(
    const char *path,
    GLuint *texture_out,
    int *width_out,
    int *height_out,
    unsigned char **pixels_out
) {
    FILE *file = fopen(path, "rb");
    char magic[3] = {0};
    int width;
    int height;
    int max_value;
    size_t row_bytes;
    size_t total_bytes;
    unsigned char *raw = NULL;
    unsigned char *flipped = NULL;
    GLuint texture = 0;

    if (!file) {
        fprintf(stderr, "Failed to open texture file: %s\n", path);
        return false;
    }

    if (fscanf(file, "%2s", magic) != 1 || magic[0] != 'P' || magic[1] != '6') {
        fprintf(stderr, "Unsupported texture format in %s\n", path);
        fclose(file);
        return false;
    }

    if (fscanf(file, "%d %d %d", &width, &height, &max_value) != 3 || max_value != 255) {
        fprintf(stderr, "Invalid texture header in %s\n", path);
        fclose(file);
        return false;
    }

    fgetc(file);
    row_bytes = (size_t) width * 3U;
    total_bytes = row_bytes * (size_t) height;
    raw = malloc(total_bytes);
    flipped = malloc(total_bytes);
    if (!raw || !flipped) {
        fprintf(stderr, "Out of memory loading %s\n", path);
        fclose(file);
        free(raw);
        free(flipped);
        return false;
    }

    if (fread(raw, 1, total_bytes, file) != total_bytes) {
        fprintf(stderr, "Failed to read texture pixels from %s\n", path);
        fclose(file);
        free(raw);
        free(flipped);
        return false;
    }
    fclose(file);

    for (int row = 0; row < height; ++row) {
        size_t src = (size_t) row * row_bytes;
        size_t dst = (size_t) (height - 1 - row) * row_bytes;
        for (size_t i = 0; i < row_bytes; ++i) {
            flipped[dst + i] = raw[src + i];
        }
    }
    free(raw);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, flipped);
    glBindTexture(GL_TEXTURE_2D, 0);

    *texture_out = texture;
    *width_out = width;
    *height_out = height;
    *pixels_out = flipped;
    return true;
}

static void draw_background(int width, int height) {
    set_ortho_projection(width, height);

    glBegin(GL_QUADS);
    glColor3f(0.01f, 0.02f, 0.06f);
    glVertex2f(0.0f, 0.0f);
    glVertex2f((float) width, 0.0f);
    glColor3f(0.03f, 0.08f, 0.18f);
    glVertex2f((float) width, (float) height);
    glVertex2f(0.0f, (float) height);
    glEnd();
}

static double wrap_longitude(double lon) {
    while (lon < -180.0) {
        lon += 360.0;
    }
    while (lon > 180.0) {
        lon -= 360.0;
    }
    return lon;
}

static float smoothstepf(float edge0, float edge1, float x) {
    float t = clampf((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static void compute_solar_position(const struct tm *utc_tm, double utc_hours, double *declination_radians, double *subsolar_lon) {
    double day_of_year = (double) utc_tm->tm_yday + 1.0;
    double gamma = 2.0 * PI / 365.0 * (day_of_year - 1.0 + (utc_hours - 12.0) / 24.0);
    double equation_of_time_minutes =
        229.18 * (
            0.000075
            + 0.001868 * cos(gamma)
            - 0.032077 * sin(gamma)
            - 0.014615 * cos(2.0 * gamma)
            - 0.040849 * sin(2.0 * gamma)
        );
    double declination =
        0.006918
        - 0.399912 * cos(gamma)
        + 0.070257 * sin(gamma)
        - 0.006758 * cos(2.0 * gamma)
        + 0.000907 * sin(2.0 * gamma)
        - 0.002697 * cos(3.0 * gamma)
        + 0.001480 * sin(3.0 * gamma);
    double utc_minutes = utc_hours * 60.0;

    *declination_radians = declination;
    *subsolar_lon = wrap_longitude((720.0 - utc_minutes - equation_of_time_minutes) / 4.0);
}

static void compute_solar_vector(const struct tm *utc_tm, double utc_hours, double *sun_x, double *sun_y, double *sun_z) {
    double declination_radians;
    double subsolar_lon;
    double cos_declination;

    compute_solar_position(utc_tm, utc_hours, &declination_radians, &subsolar_lon);
    cos_declination = cos(declination_radians);

    *sun_x = cos_declination * sin(degrees_to_radians(subsolar_lon));
    *sun_y = sin(declination_radians);
    *sun_z = cos_declination * cos(degrees_to_radians(subsolar_lon));
}

static void update_daylight_texture(AppState *app, const struct tm *utc_tm, double utc_hours, time_t now) {
    if (
        app->last_daylight_update == now
        || !app->earth_day_pixels
        || !app->earth_night_pixels
        || !app->earth_lit_pixels
    ) {
        return;
    }

    double sun_x;
    double sun_y;
    double sun_z;

    compute_solar_vector(utc_tm, utc_hours, &sun_x, &sun_y, &sun_z);

    for (int y = 0; y < app->earth_texture_height; ++y) {
        double lat = -90.0 + ((double) y + 0.5) * 180.0 / (double) app->earth_texture_height;

        for (int x = 0; x < app->earth_texture_width; ++x) {
            double lon = -180.0 + ((double) x + 0.5) * 360.0 / (double) app->earth_texture_width;
            double nx;
            double ny;
            double nz;
            double incidence;

            geo_to_cartesian(lon, lat, 1.0, &nx, &ny, &nz);
            incidence = nx * sun_x + ny * sun_y + nz * sun_z;
            float day_mix = smoothstepf(-0.05f, 0.03f, (float) incidence);
            float night_mix = 1.0f - day_mix;
            size_t index = ((size_t) y * (size_t) app->earth_texture_width + (size_t) x) * 3U;
            float day_r = (float) app->earth_day_pixels[index] / 255.0f;
            float day_g = (float) app->earth_day_pixels[index + 1] / 255.0f;
            float day_b = (float) app->earth_day_pixels[index + 2] / 255.0f;
            float night_r = (float) app->earth_night_pixels[index] / 255.0f;
            float night_g = (float) app->earth_night_pixels[index + 1] / 255.0f;
            float night_b = (float) app->earth_night_pixels[index + 2] / 255.0f;
            float lit_r = day_r * day_mix + night_r * night_mix;
            float lit_g = day_g * day_mix + night_g * night_mix;
            float lit_b = day_b * day_mix + night_b * night_mix;

            app->earth_lit_pixels[index] = (unsigned char) (clampf(lit_r, 0.0f, 1.0f) * 255.0f);
            app->earth_lit_pixels[index + 1] = (unsigned char) (clampf(lit_g, 0.0f, 1.0f) * 255.0f);
            app->earth_lit_pixels[index + 2] = (unsigned char) (clampf(lit_b, 0.0f, 1.0f) * 255.0f);
        }
    }

    glBindTexture(GL_TEXTURE_2D, app->earth_texture);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        app->earth_texture_width,
        app->earth_texture_height,
        GL_RGB,
        GL_UNSIGNED_BYTE,
        app->earth_lit_pixels
    );
    glBindTexture(GL_TEXTURE_2D, 0);
    app->last_daylight_update = now;
}

static void draw_day_night_overlay(const struct tm *utc_tm, double utc_hours) {
    double sun_x;
    double sun_y;
    double sun_z;

    compute_solar_vector(utc_tm, utc_hours, &sun_x, &sun_y, &sun_z);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    for (double lat = -90.0; lat < 90.0; lat += 2.0) {
        glBegin(GL_QUAD_STRIP);
        for (double lon = -180.0; lon <= 180.0; lon += 2.0) {
            for (int step = 0; step < 2; ++step) {
                double sample_lat = lat + (step == 0 ? 2.0 : 0.0);
                double nx;
                double ny;
                double nz;
                double incidence;
                float night_alpha;
                
                geo_to_cartesian(lon, sample_lat, 1.0, &nx, &ny, &nz);
                incidence = nx * sun_x + ny * sun_y + nz * sun_z;
                night_alpha = 0.62f * (1.0f - smoothstepf(-0.30f, -0.02f, (float) incidence));
                
                glColor4f(0.02f, 0.04f, 0.10f, night_alpha);
                emit_colored_sphere_vertex(lon, sample_lat, GLOBE_RADIUS + 0.001);
            }
        }
        glEnd();
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

static void emit_textured_sphere_vertex(double lon, double lat, double radius) {
    double x;
    double y;
    double z;
    float u = (float) ((lon + 180.0) / 360.0);
    float v = (float) ((lat + 90.0) / 180.0);

    geo_to_cartesian(lon, lat, radius, &x, &y, &z);
    glColor3f(1.0f, 1.0f, 1.0f);
    glTexCoord2f(u, v);
    glNormal3d(x / radius, y / radius, z / radius);
    glVertex3d(x, y, z);
}

static void emit_colored_sphere_vertex(double lon, double lat, double radius) {
    double x;
    double y;
    double z;

    geo_to_cartesian(lon, lat, radius, &x, &y, &z);
    glNormal3d(x / radius, y / radius, z / radius);
    glVertex3d(x, y, z);
}

static void draw_textured_globe(const AppState *app) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, app->earth_texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    for (double lat = -90.0; lat < 90.0; lat += LATITUDE_STEP) {
        glBegin(GL_QUAD_STRIP);
        for (double lon = -180.0; lon <= 180.0; lon += LONGITUDE_STEP) {
            emit_textured_sphere_vertex(lon, lat + LATITUDE_STEP, GLOBE_RADIUS);
            emit_textured_sphere_vertex(lon, lat, GLOBE_RADIUS);
        }
        glEnd();
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

static void draw_globe_graticule(void) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.46f, 0.56f, 0.70f, 0.26f);
    glLineWidth(1.0f);

    for (int lon = -180; lon <= 180; lon += 15) {
        glBegin(GL_LINE_STRIP);
        for (int lat = -90; lat <= 90; lat += 5) {
            double x;
            double y;
            double z;
            geo_to_cartesian((double) lon, (double) lat, GRID_RADIUS, &x, &y, &z);
            glNormal3d(x / GRID_RADIUS, y / GRID_RADIUS, z / GRID_RADIUS);
            glVertex3d(x, y, z);
        }
        glEnd();
    }

    for (int lat = -60; lat <= 60; lat += 30) {
        glBegin(GL_LINE_STRIP);
        for (int lon = -180; lon <= 180; lon += 5) {
            double x;
            double y;
            double z;
            geo_to_cartesian((double) lon, (double) lat, GRID_RADIUS, &x, &y, &z);
            glNormal3d(x / GRID_RADIUS, y / GRID_RADIUS, z / GRID_RADIUS);
            glVertex3d(x, y, z);
        }
        glEnd();
    }

    glDisable(GL_BLEND);
}

static void format_utc_clock(const struct tm *utc_tm, char *buffer, size_t size) {
    snprintf(buffer, size, "UTC %02d:%02d:%02d", utc_tm->tm_hour, utc_tm->tm_min, utc_tm->tm_sec);
}

static void format_offset_clock(int offset, const struct tm *utc_tm, char *buffer, size_t size) {
    int total_minutes = utc_tm->tm_hour * 60 + utc_tm->tm_min + offset * 60;
    while (total_minutes < 0) {
        total_minutes += 24 * 60;
    }
    while (total_minutes >= 24 * 60) {
        total_minutes -= 24 * 60;
    }

    snprintf(buffer, size, "%+03d %02d:%02d", offset, total_minutes / 60, total_minutes % 60);
}

static void format_declination(double declination_radians, char *buffer, size_t size) {
    double declination_degrees = declination_radians * 180.0 / PI;
    snprintf(buffer, size, "DEC %+04.1f", declination_degrees);
}

static void draw_utc_overlay(int width, int height, const struct tm *utc_tm, double declination_radians) {
    char buffer[32];

    set_ortho_projection(width, height);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glColor4f(0.00f, 0.00f, 0.00f, 0.34f);
    draw_rect(18.0f, 18.0f, 300.0f, 44.0f);
    glColor3f(0.99f, 1.00f, 1.00f);
    format_utc_clock(utc_tm, buffer, sizeof(buffer));
    draw_pixel_text(28.0f, 24.0f, 2.0f, buffer);
    format_declination(declination_radians, buffer, sizeof(buffer));
    draw_pixel_text(180.0f, 24.0f, 2.0f, buffer);

    glColor4f(0.68f, 0.84f, 0.95f, 0.40f);
    draw_outline_rect(8.0f, 8.0f, (float) width - 8.0f, (float) height - 8.0f);
}

static void transform_point(const GLdouble matrix[16], double x, double y, double z, double *out_x, double *out_y, double *out_z) {
    *out_x = matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12];
    *out_y = matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13];
    *out_z = matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14];
}

static void transform_vector(const GLdouble matrix[16], double x, double y, double z, double *out_x, double *out_y, double *out_z) {
    *out_x = matrix[0] * x + matrix[4] * y + matrix[8] * z;
    *out_y = matrix[1] * x + matrix[5] * y + matrix[9] * z;
    *out_z = matrix[2] * x + matrix[6] * y + matrix[10] * z;
}

static void draw_globe_timezone_labels(const struct tm *utc_tm) {
    char buffer[32];
    GLdouble modelview[16];

    glGetDoublev(GL_MODELVIEW_MATRIX, modelview);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (int offset = -12; offset <= 12; ++offset) {
        double lon = (double) offset * 15.0;
        double lat = 10.0;
        double px;
        double py;
        double pz;
        double nx;
        double ny;
        double nz;
        double eye_x;
        double eye_y;
        double eye_z;
        double eye_nx;
        double eye_ny;
        double eye_nz;
        double view_dot;
        double label_scale;

        geo_to_cartesian(lon, lat, LABEL_RADIUS, &px, &py, &pz);
        geo_to_cartesian(lon, lat, 1.0, &nx, &ny, &nz);
        transform_point(modelview, px, py, pz, &eye_x, &eye_y, &eye_z);
        transform_vector(modelview, nx, ny, nz, &eye_nx, &eye_ny, &eye_nz);

        view_dot = eye_nx * (-eye_x) + eye_ny * (-eye_y) + eye_nz * (-eye_z);
        label_scale = 0.52 + 0.40 * clampf((float) (view_dot / 4.0), 0.0f, 1.0f);

        format_offset_clock(offset, utc_tm, buffer, sizeof(buffer));
        glColor3f(0.99f, 1.00f, 1.00f);
        draw_surface_text(lon, lat, LABEL_RADIUS, 0.005 * label_scale, buffer);
    }

    glDisable(GL_BLEND);
}

static void render_frame(AppState *app) {
    struct timeval tv;
    time_t now;
    struct tm utc_tm;
    double utc_hours;
    double declination_radians;
    double subsolar_lon;

    gettimeofday(&tv, NULL);
    now = tv.tv_sec;
    gmtime_r(&now, &utc_tm);
    utc_hours = (double) utc_tm.tm_hour
        + (double) utc_tm.tm_min / 60.0
        + (double) utc_tm.tm_sec / 3600.0
        + (double) tv.tv_usec / 3600000000.0;
    compute_solar_position(&utc_tm, utc_hours, &declination_radians, &subsolar_lon);

    update_daylight_texture(app, &utc_tm, utc_hours, now);
    draw_background(app->width, app->height);

    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glShadeModel(GL_SMOOTH);

    set_perspective_projection(app);
    draw_textured_globe(app);
    draw_day_night_overlay(&utc_tm, utc_hours);
    draw_globe_graticule();
    draw_globe_timezone_labels(&utc_tm);
    draw_utc_overlay(app->width, app->height, &utc_tm, declination_radians);
}

static bool init_window(
    Display **display_out,
    Window *window_out,
    GLXContext *context_out,
    AppState *app
) {
    int screen;
    int attributes[] = {
        GLX_RGBA,
        GLX_DOUBLEBUFFER,
        GLX_DEPTH_SIZE, 24,
        None
    };

    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Failed to open X display.\n");
        return false;
    }

    screen = DefaultScreen(display);
    XVisualInfo *visual = glXChooseVisual(display, screen, attributes);
    if (!visual) {
        fprintf(stderr, "Failed to choose a GLX visual.\n");
        XCloseDisplay(display);
        return false;
    }

    Colormap colormap = XCreateColormap(display, RootWindow(display, screen), visual->visual, AllocNone);

    XSetWindowAttributes attributes_window;
    attributes_window.colormap = colormap;
    attributes_window.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask;

    Window window = XCreateWindow(
        display,
        RootWindow(display, screen),
        0,
        0,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        0,
        visual->depth,
        InputOutput,
        visual->visual,
        CWColormap | CWEventMask,
        &attributes_window
    );

    XStoreName(display, window, "World Globe Timezones");
    Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete, 1);
    XMapWindow(display, window);

    GLXContext context = glXCreateContext(display, visual, NULL, True);
    XFree(visual);

    if (!context) {
        fprintf(stderr, "Failed to create a GLX context.\n");
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        return false;
    }

    glXMakeCurrent(display, window, context);
    glClearColor(0.01f, 0.02f, 0.06f, 1.0f);

    app->width = WINDOW_WIDTH;
    app->height = WINDOW_HEIGHT;
    app->earth_texture = 0;
    app->earth_texture_width = 0;
    app->earth_texture_height = 0;
    app->earth_day_pixels = NULL;
    app->earth_night_pixels = NULL;
    app->earth_lit_pixels = NULL;
    app->last_daylight_update = (time_t) -1;
    app->yaw_degrees = -30.0f;
    app->pitch_degrees = 0.0f;
    app->dragging = false;
    app->last_mouse_x = 0;
    app->last_mouse_y = 0;

    if (!load_ppm_texture(
        EARTH_DAY_TEXTURE_PATH,
        &app->earth_texture,
        &app->earth_texture_width,
        &app->earth_texture_height,
        &app->earth_day_pixels
    )) {
        glXMakeCurrent(display, None, NULL);
        glXDestroyContext(display, context);
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        return false;
    }

    {
        GLuint night_texture = 0;
        int night_width = 0;
        int night_height = 0;

        if (!load_ppm_texture(
            EARTH_NIGHT_TEXTURE_PATH,
            &night_texture,
            &night_width,
            &night_height,
            &app->earth_night_pixels
        )) {
            glDeleteTextures(1, &app->earth_texture);
            free(app->earth_day_pixels);
            glXMakeCurrent(display, None, NULL);
            glXDestroyContext(display, context);
            XDestroyWindow(display, window);
            XCloseDisplay(display);
            return false;
        }

        glDeleteTextures(1, &night_texture);

        if (night_width != app->earth_texture_width || night_height != app->earth_texture_height) {
            fprintf(stderr, "Day and night textures must have matching dimensions.\n");
            glDeleteTextures(1, &app->earth_texture);
            free(app->earth_day_pixels);
            free(app->earth_night_pixels);
            glXMakeCurrent(display, None, NULL);
            glXDestroyContext(display, context);
            XDestroyWindow(display, window);
            XCloseDisplay(display);
            return false;
        }
    }

    app->earth_lit_pixels = malloc((size_t) app->earth_texture_width * (size_t) app->earth_texture_height * 3U);
    if (!app->earth_lit_pixels) {
        fprintf(stderr, "Failed to allocate daylight texture buffer.\n");
        glDeleteTextures(1, &app->earth_texture);
        free(app->earth_day_pixels);
        free(app->earth_night_pixels);
        glXMakeCurrent(display, None, NULL);
        glXDestroyContext(display, context);
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        return false;
    }

    *display_out = display;
    *window_out = window;
    *context_out = context;
    return true;
}

int main(void) {
    Display *display = NULL;
    Window window = 0;
    GLXContext context = NULL;
    AppState app = {WINDOW_WIDTH, WINDOW_HEIGHT, 0, 0, 0, NULL, NULL, NULL, (time_t) -1, -30.0f, 0.0f, false, 0, 0};
    bool running = true;

    if (!init_window(&display, &window, &context, &app)) {
        return 1;
    }

    while (running) {
        while (XPending(display) > 0) {
            XEvent event;
            XNextEvent(display, &event);

            switch (event.type) {
                case ConfigureNotify:
                    app.width = event.xconfigure.width;
                    app.height = event.xconfigure.height;
                    break;
                case ButtonPress:
                    if (event.xbutton.button == Button1) {
                        app.dragging = true;
                        app.last_mouse_x = event.xbutton.x;
                        app.last_mouse_y = event.xbutton.y;
                    }
                    break;
                case ButtonRelease:
                    if (event.xbutton.button == Button1) {
                        app.dragging = false;
                    }
                    break;
                case MotionNotify:
                    if (app.dragging) {
                        int dx = event.xmotion.x - app.last_mouse_x;
                        int dy = event.xmotion.y - app.last_mouse_y;
                        app.yaw_degrees += (float) dx * ROTATION_SENSITIVITY;
                        app.pitch_degrees = clampf(
                            app.pitch_degrees + (float) dy * ROTATION_SENSITIVITY,
                            -20.0f,
                            20.0f
                        );
                        app.last_mouse_x = event.xmotion.x;
                        app.last_mouse_y = event.xmotion.y;
                    }
                    break;
                case KeyPress: {
                    KeySym key = XLookupKeysym(&event.xkey, 0);
                    if (key == XK_Escape || key == XK_q) {
                        running = false;
                    } else if (key == XK_Right) {
        			    app.yaw_degrees += 0.5;
        		    } else if (key == XK_Left) {
        			    app.yaw_degrees -= 0.5;
		    }
		    break;
                }
                case ClientMessage:
                    running = false;
                    break;
                default:
                    break;
            }
        }

        render_frame(&app);
        glXSwapBuffers(display, window);

        struct timespec sleep_for = {0, 16 * 1000 * 1000};
        nanosleep(&sleep_for, NULL);
    }

    if (app.earth_texture != 0) {
        glDeleteTextures(1, &app.earth_texture);
    }
    free(app.earth_day_pixels);
    free(app.earth_night_pixels);
    free(app.earth_lit_pixels);

    glXMakeCurrent(display, None, NULL);
    glXDestroyContext(display, context);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}
