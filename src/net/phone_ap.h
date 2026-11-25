#pragma once
#include "net/phone_link.h"

namespace phone_ap {
bool start();
void stop();
void service();
phone::Link& link();
}
