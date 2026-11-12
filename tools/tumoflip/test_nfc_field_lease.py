"""No peripheral writes: fake leases verify partial-failure cleanup order."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/nfc_field_sniffer"


class NfcFieldLeaseTests(unittest.TestCase):
    def test_failed_and_repeated_capture_does_not_release_someone_elses_resource(self):
        production = ""
        for name in ("nfc_field_lease.h", "nfc_field_lease.c"):
            if (APP / name).exists():
                production += re.sub(r"^#(?:include|pragma).*\n", "\n", (APP / name).read_text(), flags=re.M) + "\n"
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
''' + production + r'''
typedef struct{int fail;unsigned owned,drops;unsigned released[4];}Driver;
static bool take(void*c,NfcFieldResource r){Driver*d=c;if((int)r==d->fail)return false;assert(!(d->owned&(1u<<r)));d->owned|=1u<<r;return true;}
static void drop(void*c,NfcFieldResource r){Driver*d=c;assert(d->owned&(1u<<r));d->owned&=~(1u<<r);d->released[d->drops++]=r;}
int main(void){
 const NfcFieldLeaseOps ops={take,drop};
 for(int fail=0;fail<4;fail++){Driver d={.fail=fail};NfcFieldLease l={0};assert(!nfc_field_lease_acquire(&l,&ops,&d));assert(!d.owned&&d.drops==(unsigned)fail);
  for(unsigned i=0;i<d.drops;i++)assert(d.released[i]==(unsigned)(fail-1)-(unsigned)i);
  nfc_field_lease_release(&l);assert(d.drops==(unsigned)fail);
 }
 Driver d={.fail=-1};NfcFieldLease l={0};assert(nfc_field_lease_acquire(&l,&ops,&d)&&d.owned==15);
 assert(!nfc_field_lease_acquire(&l,&ops,&d)&&d.owned==15);nfc_field_lease_release(&l);assert(!d.owned&&d.drops==4);nfc_field_lease_release(&l);assert(d.drops==4);
 assert(!nfc_field_lease_acquire(&l,NULL,&d));return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
