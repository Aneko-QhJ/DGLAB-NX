#include <assert.h>
#include <stdio.h>
#include <dglab/ui/presentation.h>
int main(void) {
    for (int resolution = 0; resolution < 2; ++resolution) {
        float scale = resolution ? 1.5f : 1.0f;
        for (unsigned version = 1; version <= 10; ++version) {
            unsigned modules = 17 + version * 4;
            DglabQrPixels qr;
            float x = 54.25f * scale, y = 118.75f * scale;
            assert(dglabQrPixels(x, y, 350 * scale, 300 * scale, modules, 8 * scale, 4 * scale, &qr));
            assert(qr.module >= (int)(4 * scale) && qr.module <= (int)(8 * scale));
            assert(qr.side == ((int)modules + 8) * qr.module);
            assert(qr.x >= x && qr.y >= y);
            assert(qr.x + qr.side <= x + 350 * scale && qr.y + qr.side <= y + 300 * scale);
        }
    }
    DglabQrPixels qr;
    assert(!dglabQrPixels(0, 0, 10, 10, 57, 8, 4, &qr));
    assert(!dglabQrPixels(0, 0, 350, 300, 0, 8, 4, &qr));
    assert(!dglabQrPixels(0, 0, 350, 300, 57, 8, 4, NULL));
    puts("720p/1080p QR integer pixel, containment and quiet-zone layout tests passed");
}
