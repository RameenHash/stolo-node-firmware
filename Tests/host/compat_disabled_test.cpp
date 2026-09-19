// Product switch is independently compiled; software verified.
#define STOLO_ENABLE_FACTORY_COMPAT 0
#include "firmware_env.h"
int main() {
  boot_unowned(); CHECK(stolo_role()==STOLO_ROLE_GUEST && !stolo_kiss_gate(CMD_CFG_READ), "build switch disables factory compat");
  scp(SCP_HELLO); scp(SCP_ENROLL,std::vector<uint8_t>(96,0x12));
  CHECK(replied(SCP_ENROLL|SCP_REPLY), "SCP enrollment remains available with compat disabled");
  printf("%d passed, %d failed\n",passes,failures); return failures?1:0;
}
