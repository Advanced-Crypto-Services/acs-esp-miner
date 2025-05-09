#ifndef SYSTEM_H_
#define SYSTEM_H_

#include "global_state.h"
#include "esp_netif.h"

extern esp_netif_t * netif;

void SYSTEM_init_system(GlobalState * GLOBAL_STATE);
void SYSTEM_init_peripherals(GlobalState * GLOBAL_STATE);
void SYSTEM_task(void * pvParameters);

void SYSTEM_notify_accepted_share(GlobalState * GLOBAL_STATE);
void SYSTEM_notify_rejected_share(GlobalState * GLOBAL_STATE);
void SYSTEM_notify_found_nonce(GlobalState * GLOBAL_STATE, double found_diff, uint8_t job_id);
void SYSTEM_notify_mining_started(GlobalState * GLOBAL_STATE);
void SYSTEM_notify_new_ntime(GlobalState * GLOBAL_STATE, uint32_t ntime);

#endif /* SYSTEM_H_ */
