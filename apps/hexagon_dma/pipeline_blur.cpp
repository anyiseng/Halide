#include "Halide.h"

using namespace Halide;

// Define a 1D Gaussian blur (a [1 4 6 4 1] filter) of 5 elements.
Expr blur5(Expr x0, Expr x1, Expr x2, Expr x3, Expr x4) {
    // Widen to 16 bits, so we don't overflow while computing the stencil.
    x0 = cast<uint16_t>(x0);
    x1 = cast<uint16_t>(x1);
    x2 = cast<uint16_t>(x2);
    x3 = cast<uint16_t>(x3);
    x4 = cast<uint16_t>(x4);
    return cast<uint8_t>((x0 + 4*x1 + 6*x2 + 4*x3 + x4 + 8)/16);
}


class DmaPipeline : public Generator<DmaPipeline> {
public:
    Input<Buffer<uint8_t>> input{"input", 2};
    Output<Buffer<uint8_t>> output{"output", 2};

    void generate() {
        Var x{"x"}, y{"y"};

        // We need a wrapper for the output so we can schedule the
        // multiply update in tiles.
        Func copy("copy");
        Func input_bounded("input_bounded");
        Func blur_y{"blur_y"};

        input_bounded(x, y) = BoundaryConditions::repeat_edge(input)(x, y);

        copy(x, y) = input_bounded(x, y);
        blur_y(x, y) = blur5(copy(x, y - 2),
                                copy(x, y - 1),
                                copy(x, y    ),
                                copy(x, y + 1),
                                copy(x, y + 2));
        output(x, y) = blur5(blur_y(x - 2, y),
                              blur_y(x - 1, y),
                              blur_y(x,     y),
                              blur_y(x + 1, y),
                              blur_y(x + 2, y));
        
        

        // output(x, y) = copy(x, y) * 2;

        Var tx("tx"), ty("ty");

        // Break the output into tiles.
        const int tile_width = 256;
        const int tile_height = 128;

        Expr fac = output.dim(1).extent()/2;

        Var yo, yi;
        input_bounded.compute_root();

        output.split(y, yo, yi, fac);

        output.compute_root()
              .tile(x, yi, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp)
              .parallel(yo);

        // Schedule the copy to be computed at tiles with a
        // circular buffer of two tiles.
        copy.compute_at(output, tx)
            .store_at(output, tx)
            .copy_to_host();
    }

};

HALIDE_REGISTER_GENERATOR(DmaPipeline, dma_pipeline_blur)
