/*******************************************************************************/
// Original LuaJIT code for testing purpose

#include <QtDebug>
#include <lj_bc.h>

typedef uint32_t BCPos;
typedef uint8_t BCReg;

/* Read ULEB128 value. */
static uint32_t debug_read_uleb128(const uint8_t **pp)
{
  const uint8_t *p = *pp;
  uint32_t v = *p++;
  if (LJ_UNLIKELY(v >= 0x80)) {
    int sh = 0;
    v &= 0x7f;
    do { v |= ((*p & 0x7f) << (sh += 7)); } while (*p++ >= 0x80);
  }
  *pp = p;
  return v;
}

/* Fixed internal variable names. */
#define VARNAMEDEF(_) \
  _(FOR_IDX, "(for index)") \
  _(FOR_STOP, "(for limit)") \
  _(FOR_STEP, "(for step)") \
  _(FOR_GEN, "(for generator)") \
  _(FOR_STATE, "(for state)") \
  _(FOR_CTL, "(for control)")

enum {
  VARNAME_END,
#define VARNAMEENUM(name, str)	VARNAME_##name,
  VARNAMEDEF(VARNAMEENUM)
#undef VARNAMEENUM
  VARNAME__MAX
};

/* Get name of a local variable from slot number and PC. */
const char *debug_varname(const uint8_t *p, BCPos pc, BCReg slot)
{
    qDebug() << "debug_varname" << pc << slot;
    if (p) {
        BCPos lastpc = 0;
        for (;;) {
            const char *name = (const char *)p;
            uint32_t vn = *p++;
            BCPos startpc, endpc;
            if (vn < VARNAME__MAX) {
                if (vn == VARNAME_END)
                    break;  /* End of varinfo. */
            } else {
                while (*p++)
                    ;  /* Skip over variable name string. */
            }
            lastpc = startpc = lastpc + debug_read_uleb128(&p);
            if (startpc > pc)
                break;
            endpc = startpc + debug_read_uleb128(&p);
            qDebug() << "startpc" << startpc << "endpc" << endpc <<
                        ( vn < VARNAME__MAX ? QByteArray::number(vn) : QByteArray(name) );
            if (pc < endpc && slot-- == 0) {
                if (vn < VARNAME__MAX) {
#if 1
#define VARNAMESTR(name, str)	str "\0"
                    name = VARNAMEDEF(VARNAMESTR);
#undef VARNAMESTR
                    if (--vn)
                        while (*name++ || --vn)
                            ;
#else
                    return s_varname[vn];
#endif
                }
                qDebug() << "found" << name;
                return name;
            }
        }
    }
    qDebug() << "found NULL";
    return NULL;
}

