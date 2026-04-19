//typedef enum
//{
//    LM_STATE_ALL_ACTIVE = 0,
//    LM_STATE_SOME_ACTIVE,
//    LM_STATE_NONE_ACTIVE
//} LoadManageState_t;
//
//typedef enum
//{
//    EVT_NONE = 0,
//    EVT_SWITCH_CHANGED,
//    EVT_BECOME_UNSTABLE,
//    EVT_BECOME_STABLE,
//    EVT_TIMER_SHED,
//    EVT_TIMER_RECOVER
//} LoadCtrlEvent_t;
//
//static void T_LoadCtrl(void *pvParameters)
//{
//    LoadCtrlMessage receivedMsg;
//    LoadManageState_t lmState = LM_STATE_ALL_ACTIVE;
//    uint8_t first_shed_tag = 0;
//
//    (void) pvParameters;
//    for (;;)
//    {
//        if (xQueueReceive(Q_newLoadCtrl, &receivedMsg, portMAX_DELAY) != pdPASS)
//        {
//            continue;
//        }
//
//        LoadCtrlEvent_t event = EVT_NONE;
//        if (receivedMsg.producer_id == PRODUCER_SWITCH)
//        {
//            uint8_t newMask = receivedMsg.switch_state & 0x1F;
//
//            if (current_system_mode == LOAD_MANAGING)
//            {
//                /* In load managing mode:
//                 * user can turn OFF a load immediately,
//                 * but cannot force ON a shed load by switch.
//                 */
//                if ((userLoadMask == 0) && (shedByRelayMask == 0))
//                {
//                    userLoadMask = newMask;
//                }
//                else
//                {
//                    userLoadMask &= newMask;
//                }
//            }
//            else
//            {
//                /* In maintenance mode:
//                 * switch directly controls loads.
//                 */
//                userLoadMask = newMask;
//            }
//
//            event = EVT_SWITCH_CHANGED;
//        }
//        else if (receivedMsg.producer_id == PRODUCER_STABILITY)
//        {
//            prevNetworkUnstable = currentNetworkUnstable;
//            currentNetworkUnstable = receivedMsg.stability_state;
//
//            if ((prevNetworkUnstable == 0) && (currentNetworkUnstable == 1))
//            {
//                event = EVT_BECOME_UNSTABLE;
//            }
//            else if ((prevNetworkUnstable == 1) && (currentNetworkUnstable == 0))
//            {
//                event = EVT_BECOME_STABLE;
//            }
//            else
//            {
//                event = EVT_NONE;
//            }
//        }
//        else if (receivedMsg.producer_id == PRODUCER_TIMER)
//        {
//            if (receivedMsg.shed_or_recover == 1)
//            {
//                event = EVT_TIMER_SHED;
//            }
//            else if (receivedMsg.shed_or_recover == 2)
//            {
//                event = EVT_TIMER_RECOVER;
//            }
//        }
//        /* -------------------------------------------------
//         * 2) MAINTENANCE mode = direct output mode
//         * ------------------------------------------------- */
//        if (current_system_mode == MAINTENANCE)
//        {
//            xTimerStop(xManageTimer, 0);
//            waitingForTimer = 0;
//            shedByRelayMask = 0;
//
//            lmState = prvGetLoadManageState(userLoadMask, shedByRelayMask);
//            prvPushLedOutputs(userLoadMask, shedByRelayMask);
//            continue;
//        }
//
//
//
//        /* -------------------------------------------------
//         * 3) LOAD_MANAGING mode: Moore actions by state
//         * ------------------------------------------------- */
//        lmState = prvGetLoadManageState(userLoadMask, shedByRelayMask);
//        first_shed_tag = 0;
//
//        switch (lmState)
//        {
//            case LM_STATE_ALL_ACTIVE:
//            {
//                switch (event)
//                {
//                    case EVT_BECOME_UNSTABLE:
//                    {
//                        uint8_t oldEffective = userLoadMask & (~shedByRelayMask) & 0x1F;
//
//                        shedByRelayMask = addLowestPriorityShed(userLoadMask, shedByRelayMask);
//
//                        uint8_t newEffective = userLoadMask & (~shedByRelayMask) & 0x1F;
//
//                        if (oldEffective != newEffective)
//                        {
//                            first_shed_tag = 1;
//                            waitingForTimer = 1;
//                            xTimerReset(xManageTimer, 0);
//                        }
//                        else
//                        {
//                            waitingForTimer = 0;
//                            xTimerStop(xManageTimer, 0);
//                        }
//                        break;
//                    }
//
//                    case EVT_SWITCH_CHANGED:
//                    case EVT_BECOME_STABLE:
//                    case EVT_TIMER_RECOVER:
//                    case EVT_TIMER_SHED:
//                    default:
//                        /* no action */
//                        break;
//                }
//                break;
//            }
//
//            case LM_STATE_SOME_ACTIVE:
//            {
//                switch (event)
//                {
//                    case EVT_BECOME_UNSTABLE:
//                    {
//                        /* stay unstable for 500ms -> more shedding */
//                        waitingForTimer = 1;
//                        xTimerReset(xManageTimer, 0);
//                        break;
//                    }
//
//                    case EVT_BECOME_STABLE:
//                    {
//                        /* stay stable for 500ms -> recovery */
//                        waitingForTimer = 1;
//                        xTimerReset(xManageTimer, 0);
//                        break;
//                    }
//
//                    case EVT_TIMER_SHED:
//                    {
//                        uint8_t oldEffective = userLoadMask & (~shedByRelayMask) & 0x1F;
//
//                        shedByRelayMask = addLowestPriorityShed(userLoadMask, shedByRelayMask);
//
//                        uint8_t newEffective = userLoadMask & (~shedByRelayMask) & 0x1F;
//
//                        if (oldEffective != newEffective)
//                        {
//                            waitingForTimer = 1;
//                            xTimerReset(xManageTimer, 0);
//                        }
//                        else
//                        {
//                            waitingForTimer = 0;
//                            xTimerStop(xManageTimer, 0);
//                        }
//                        break;
//                    }
//
//                    case EVT_TIMER_RECOVER:
//                    {
//                        uint8_t oldShed = shedByRelayMask;
//
//                        shedByRelayMask = recoverHighestPriorityShed(userLoadMask, shedByRelayMask);
//
//                        if (oldShed != shedByRelayMask)
//                        {
//                            waitingForTimer = 1;
//                            xTimerReset(xManageTimer, 0);
//                        }
//                        else
//                        {
//                            waitingForTimer = 0;
//                            xTimerStop(xManageTimer, 0);
//                        }
//                        break;
//                    }
//
//                    case EVT_SWITCH_CHANGED:
//                    default:
//                        /* no timer policy change here */
//                        break;
//                }
//                break;
//            }
//
//            case LM_STATE_NONE_ACTIVE:
//            {
//                switch (event)
//                {
//                    case EVT_BECOME_STABLE:
//                    {
//                        waitingForTimer = 1;
//                        xTimerReset(xManageTimer, 0);
//                        break;
//                    }
//
//                    case EVT_TIMER_RECOVER:
//                    {
//                        uint8_t oldShed = shedByRelayMask;
//
//                        shedByRelayMask = recoverHighestPriorityShed(userLoadMask, shedByRelayMask);
//
//                        if (oldShed != shedByRelayMask)
//                        {
//                            waitingForTimer = 1;
//                            xTimerReset(xManageTimer, 0);
//                        }
//                        else
//                        {
//                            waitingForTimer = 0;
//                            xTimerStop(xManageTimer, 0);
//                        }
//                        break;
//                    }
//
//                    case EVT_BECOME_UNSTABLE:
//                    case EVT_TIMER_SHED:
//                    case EVT_SWITCH_CHANGED:
//                    default:
//                        /* cannot shed further */
//                        break;
//                }
//                break;
//            }
//
//            default:
//                break;
//        }
//
//        /* -------------------------------------------------
//         * 4) Output logic (Moore output)
//         * ------------------------------------------------- */
//        lmState = prvGetLoadManageState(userLoadMask, shedByRelayMask);
//        prvPushLedOutputs(userLoadMask, shedByRelayMask);
//
//        if (first_shed_tag)
//        {
//            end = timer1us_now();
//            first_respond_time = timer1us_elapsed(start, end) / 1000.0;
//            updateRecordTime(first_respond_time);
//        }
//    }
//}
//static LoadManageState_t prvGetLoadManageState(uint8_t userMask, uint8_t shedMask)
//{
//    uint8_t effective = userMask & (~shedMask) & 0x1F;
//
//    if (effective == 0x00)
//    {
//        return LM_STATE_NONE_ACTIVE;
//    }
//    else if (effective == (userMask & 0x1F))
//    {
//        return LM_STATE_ALL_ACTIVE;
//    }
//    else
//    {
//        return LM_STATE_SOME_ACTIVE;
//    }
//}
//
//static void prvPushLedOutputs(uint8_t userMask, uint8_t shedMask)
//{
//    uint8_t redMask   = userMask & (~shedMask) & 0x1F;   // active loads
//    uint8_t greenMask = shedMask & userMask & 0x1F;      // shed loads
//
//    effectiveLoadMask = redMask;
//
//    xQueueSendToBack(Q_newRedLed, &redMask, portMAX_DELAY);
//    xQueueSendToBack(Q_newGreenLed, &greenMask, portMAX_DELAY);
//}
