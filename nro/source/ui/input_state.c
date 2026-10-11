#include <dglab/ui/input_state.h>
#include <string.h>
void dglabUiKeysReset(DglabUiKeys* keys, uint64_t held)
{
    memset(keys, 0, sizeof(*keys));
    keys->blocked = held;
}
void dglabUiKeysUpdate(DglabUiKeys* keys, uint64_t raw)
{
    keys->blocked &= raw;
    keys->held = raw & ~keys->blocked;
    keys->down = keys->held & ~keys->previous;
    keys->previous = keys->held;
}
void dglabUiRepeatReset(DglabUiRepeat* repeat)
{
    memset(repeat, 0, sizeof(*repeat));
}
int dglabUiRepeatStep(DglabUiRepeat* repeat, int press, int held, uint64_t now)
{
    if (!held) {
        dglabUiRepeatReset(repeat);
        return press;
    }
    if (press || repeat->direction != held) {
        repeat->direction = held;
        repeat->started = now;
        repeat->repeated = 0;
        return press;
    }
    if (now - repeat->started >= 500000000ull &&
        (!repeat->repeated || now - repeat->repeated >= 200000000ull)) {
        repeat->repeated = now;
        return held;
    }
    return 0;
}
