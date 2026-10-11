#include <dglab/ui/input_state.h>
#include <assert.h>
#include <stdio.h>

static void boundaries(void)
{
    DglabUiKeys keys;
    dglabUiKeysReset(&keys, 1); // A entering a page is still held.
    dglabUiKeysUpdate(&keys, 1);
    assert(!keys.down && !keys.held);
    dglabUiKeysUpdate(&keys, 3); // Another key is usable while A is blocked.
    assert(keys.down == 2 && keys.held == 2);
    dglabUiKeysUpdate(&keys, 2);
    assert(!keys.down && keys.held == 2);
    dglabUiKeysUpdate(&keys, 3);
    assert(keys.down == 1 && keys.held == 3);
    dglabUiKeysReset(&keys, 3); // B return / menu handoff blocks all held keys.
    dglabUiKeysUpdate(&keys, 3);
    assert(!keys.down && !keys.held);
    dglabUiKeysUpdate(&keys, 0);
    dglabUiKeysUpdate(&keys, 2);
    assert(keys.down == 2 && keys.held == 2);
}
static void parameters(void)
{
    DglabUiRepeat r;
    dglabUiRepeatReset(&r);
    assert(dglabUiRepeatStep(&r, 1, 1, 1000000000ull) == 1);
    assert(!dglabUiRepeatStep(&r, 0, 1, 1499999999ull));
    assert(dglabUiRepeatStep(&r, 0, 1, 1500000000ull) == 1);
    assert(!dglabUiRepeatStep(&r, 0, 1, 1699999999ull));
    assert(dglabUiRepeatStep(&r, 0, 1, 1700000000ull) == 1);
    assert(dglabUiRepeatStep(&r, 0, 1, 4000000000ull) == 1); // no catch-up jump
    dglabUiRepeatReset(&r); // focus moved: old hold cannot instantly edit next row
    assert(!dglabUiRepeatStep(&r, 0, 1, 4000000000ull));
    assert(!dglabUiRepeatStep(&r, 0, 1, 4499999999ull));
    assert(dglabUiRepeatStep(&r, 0, 1, 4500000000ull) == 1);
    assert(dglabUiRepeatStep(&r, -1, -1, 4600000000ull) == -1);
    assert(!dglabUiRepeatStep(&r, 0, -1, 5099999999ull));
    assert(!dglabUiRepeatStep(&r, 0, 0, 5100000000ull)); // release / opposed keys
    assert(dglabUiRepeatStep(&r, 1, 1, 5200000000ull) == 1);
}
int main(void)
{
    boundaries(); parameters();
    puts("UI handoff and parameter repeat tests passed");
}
