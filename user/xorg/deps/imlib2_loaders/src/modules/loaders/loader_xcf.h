#ifndef LOADER_XCF_H
#define LOADER_XCF_H

/* Stuff for layer merging: */
extern void         combine_pixels_normal(const uint32_t * src, int src_w,
                                          int src_h, uint32_t * dest,
                                          int dest_w, int dest_h, int dest_x,
                                          int dest_y);
extern void         combine_pixels_add(const uint32_t * src, int src_w,
                                       int src_h, uint32_t * dest, int dest_w,
                                       int dest_h, int dest_x, int dest_y);
extern void         combine_pixels_sub(const uint32_t * src, int src_w,
                                       int src_h, uint32_t * dest, int dest_w,
                                       int dest_h, int dest_x, int dest_y);
extern void         combine_pixels_diff(const uint32_t * src, int src_w,
                                        int src_h, uint32_t * dest, int dest_w,
                                        int dest_h, int dest_x, int dest_y);
extern void         combine_pixels_darken(const uint32_t * src, int src_w,
                                          int src_h, uint32_t * dest,
                                          int dest_w, int dest_h, int dest_x,
                                          int dest_y);
extern void         combine_pixels_lighten(const uint32_t * src, int src_w,
                                           int src_h, uint32_t * dest,
                                           int dest_w, int dest_h, int dest_x,
                                           int dest_y);
extern void         combine_pixels_mult(const uint32_t * src, int src_w,
                                        int src_h, uint32_t * dest, int dest_w,
                                        int dest_h, int dest_x, int dest_y);
extern void         combine_pixels_div(const uint32_t * src, int src_w,
                                       int src_h, uint32_t * dest, int dest_w,
                                       int dest_h, int dest_x, int dest_y);
extern void         combine_pixels_screen(const uint32_t * src, int src_w,
                                          int src_h, uint32_t * dest,
                                          int dest_w, int dest_h, int dest_x,
                                          int dest_y);
extern void         combine_pixels_overlay(const uint32_t * src, int src_w,
                                           int src_h, uint32_t * dest,
                                           int dest_w, int dest_h, int dest_x,
                                           int dest_y);
extern void         combine_pixels_hue(const uint32_t * src, int src_w,
                                       int src_h, uint32_t * dest, int dest_w,
                                       int dest_h, int dest_x, int dest_y);
extern void         combine_pixels_sat(const uint32_t * src, int src_w,
                                       int src_h, uint32_t * dest, int dest_w,
                                       int dest_h, int dest_x, int dest_y);
extern void         combine_pixels_val(const uint32_t * src, int src_w,
                                       int src_h, uint32_t * dest, int dest_w,
                                       int dest_h, int dest_x, int dest_y);
extern void         combine_pixels_col(const uint32_t * src, int src_w,
                                       int src_h, uint32_t * dest, int dest_w,
                                       int dest_h, int dest_x, int dest_y);
extern void         combine_pixels_diss(const uint32_t * src, int src_w,
                                        int src_h, uint32_t * dest, int dest_w,
                                        int dest_h, int dest_x, int dest_y);

#endif /* LOADER_XCF_H */
