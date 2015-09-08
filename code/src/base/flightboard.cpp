/**
 * @file flightboard.cpp
 * @brief Controls the output to the flight board (Pixhawk).
 * Requires the Pixhawk to be running ArduCopter 3.3-rc10 or later.
 * Control is performed via the MAVLink interface.
 */

#include "common.h"
#include "flightboard.h"
#include "flightboard-private.h"
#include "navigation.h"
#include "gps_mav.h"
#include "imu_feed.h"

using namespace picopter::navigation;
using picopter::FlightBoard;
using picopter::GPS;
using picopter::IMU;
using std::this_thread::sleep_for;
using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::steady_clock;
using std::chrono::duration_cast;

/**
 * Constructor; initiates a connection to the flight computer.
 * @param opts A pointer to options, if any (NULL for defaults)
 * @throws std::invalid_argument if it can't connect to the flight computer.
 */
FlightBoard::FlightBoard(Options *opts)
: m_heartbeat_timeout(HEARTBEAT_TIMEOUT_DEFAULT)
, m_last_heartbeat(999)
, m_shutdown{false}
, m_disable_local{false}
, m_system_id(0)
, m_component_id(0)
, m_flightboard_id(128) //Arbitrary value 0-255
, m_is_auto_mode{false}
, m_is_rtl{false}
, m_is_in_air{false}
, m_is_armed{false}
, m_gimbal{}
, m_handler_table{}
{
    if (opts) {
        opts->SetFamily("FLIGHTBOARD");
        m_heartbeat_timeout = opts->GetInt("HEARTBEAT_TIMEOUT", HEARTBEAT_TIMEOUT_DEFAULT);
    }
    //Some increment past the timeout
    m_last_heartbeat = m_heartbeat_timeout + 10;
    //m_link = new MAVCommsSerial("/dev/ttyAMA0", 115200);
    try {
        m_link = new MAVCommsTCP("127.0.0.1", 5760);
    } catch (std::invalid_argument e) {
        m_link = new MAVCommsSerial("/dev/ttyAMA0", 115200);
    }
    
    m_gps = new GPSMAV(this, opts);
    m_imu = new IMU(this, opts);
    
    //m_link = new MAVCommsSerial("/dev/virtualcom0", 57600);
    m_input_thread = std::thread(&FlightBoard::InputLoop, this);
    m_output_thread = std::thread(&FlightBoard::OutputLoop, this);
}

/** 
 * Constructor. Constructs a new flight board with default settings.
 */
FlightBoard::FlightBoard() : FlightBoard(NULL) {}

/**
 * Destructor.
 */
FlightBoard::~FlightBoard() {
    Stop();
    m_shutdown = true;
    m_input_thread.join();
    m_output_thread.join();
    delete m_gps;
    delete m_imu;
    delete m_link;
}

GPS* FlightBoard::GetGPSInstance() {
    return m_gps;
}

IMU* FlightBoard::GetIMUInstance() {
    return m_imu;
}

void FlightBoard::GetGimbalPose(EulerAngle *p) {
    std::lock_guard<std::mutex> lock(m_gimbal_mutex);
    *p = m_gimbal;
}

void FlightBoard::InputLoop() {
    auto last_heartbeat = steady_clock::now() - seconds(m_heartbeat_timeout);
    mavlink_message_t msg;
    mavlink_heartbeat_t heartbeat;
    
    while (!m_shutdown) {
        if (m_link->ReadMessage(&msg)) {
            switch (msg.msgid) {
                case MAVLINK_MSG_ID_HEARTBEAT: {
                    mavlink_msg_heartbeat_decode(&msg, &heartbeat);
                    
                    //Skip heartbeats that aren't from the copter
                    if (heartbeat.type != MAV_TYPE_GCS) {
                        m_is_auto_mode = (heartbeat.custom_mode == GUIDED);
                        m_is_rtl = (heartbeat.custom_mode == RTL);
                        m_is_in_air = (heartbeat.system_status == MAV_STATE_ACTIVE);
                        m_is_armed = static_cast<bool>(
                            heartbeat.base_mode & MAV_MODE_FLAG_SAFETY_ARMED);
                        LogSimple(LOG_DEBUG, "Heartbeat! Mode: %d, %d, %d, %d, %d", 
                        heartbeat.type, heartbeat.base_mode, heartbeat.custom_mode, 
                        heartbeat.system_status, (int)m_is_auto_mode);
                        
                        if (m_last_heartbeat >= m_heartbeat_timeout) {
                            mavlink_message_t smsg;

                            m_system_id = msg.sysid;
                            m_component_id = msg.compid;
                            Log(LOG_INFO, "Initialisation: sysid: %d, compid: %d",
                                msg.sysid, msg.compid);
                            
                            //10 Hz update rate
                            mavlink_msg_request_data_stream_pack(
                                m_system_id, m_flightboard_id, &smsg,
                                msg.sysid, msg.compid, MAV_DATA_STREAM_ALL, 10, 1);
                            //Log(LOG_DEBUG, "Sending data request");
                            m_link->WriteMessage(&smsg);
                        }
                        last_heartbeat = steady_clock::now();
                    }
                } break;
                case MAVLINK_MSG_ID_SYS_STATUS: {
                    mavlink_sys_status_t status;
                    mavlink_msg_sys_status_decode(&msg, &status);
                    //LogSimple(LOG_DEBUG, "BATTERY: %.2fV, Draw: %.2fA, Remain: %3d%%",
                    //    status.voltage_battery*1e-3,
                    //    status.current_battery*1e-2,
                    //    status.battery_remaining);
                } break;
                case MAVLINK_MSG_ID_COMMAND_ACK: {
                    mavlink_command_ack_t ack;
                    mavlink_msg_command_ack_decode(&msg, &ack);
                    if (ack.result != 0 || ack.command != 115)
                        Log(LOG_DEBUG, "COMMAND: %d, RESULT: %d", ack.command, ack.result);
                } break;
                case MAVLINK_MSG_ID_MOUNT_STATUS: {
                    mavlink_mount_status_t mnt;
                    mavlink_msg_mount_status_decode(&msg, &mnt);
                    std::lock_guard<std::mutex> lock(m_gimbal_mutex);
                    m_gimbal.pitch = mnt.pointing_a/100.0;
                    m_gimbal.roll = mnt.pointing_b/100.0;
                    m_gimbal.yaw = mnt.pointing_c/100.0;
                    Log(LOG_DEBUG, "GOT MOUNT! %1f, %.1f, %.1f", m_gimbal.pitch, m_gimbal.roll, m_gimbal.yaw);
                } break;
                //case MAVLINK_MSG_ID_VFR_HUD: {
                //    mavlink_vfr_hud_t vfr;
                //    mavlink_msg_vfr_hud_decode(&msg, &vfr);
                //    Log(LOG_DEBUG, "Alt: %.1f m", vfr.alt);
                //} break;
                //case MAVLINK_MSG_ID_MISSION_ITEM_REACHED: {
                //    Log(LOG_DEBUG, "MID REACHED!!!!!");
                //} break;
                //case MAVLINK_MSG_ID_GLOBAL_POSITION_INT: {
                //    mavlink_global_position_int_t pos;
                //    mavlink_msg_global_position_int_decode(&msg, &pos);
                //    LogSimple(LOG_DEBUG, "Lat: %.2f, Lon: %.2f, RelAlt: %.2f, Brng: %.2f, (%.1f,%.1f,%.1f)",
                //    pos.lat*1e-7, pos.lon*1e-7, pos.relative_alt*1e-3, pos.hdg*1e-2,
                //    pos.vx*1e-2,pos.vy*1e-2,pos.vz*1e-2);
                //} break;
                //case MAVLINK_MSG_ID_GPS_RAW_INT: {
                //    mavlink_gps_raw_int_t gps;
                //    printf("GPS!\n");
                //    mavlink_msg_gps_raw_int_decode(&msg, &gps);
                //    printf("Lat: %.2f, Lon: %.2f, Alt: %.2fm\n",
                //        gps.lat * 1e-7, gps.lon * 1e-7, gps.alt / 1000.0);
                //} break;
                //case MAVLINK_MSG_ID_ATTITUDE: {
                //    mavlink_attitude_t att;
                //    mavlink_msg_attitude_decode(&msg, &att);
                //    m_current_yaw = att.yaw;
                //    LogSimple(LOG_DEBUG, "YAW: %.2f", RAD2DEG(att.yaw));
                //    LogSimple(LOG_DEBUG, "Roll: %.2f, Pitch: %.2f, Yaw: %.2f",
                //        RAD2DEG(att.roll), RAD2DEG(att.pitch), RAD2DEG(att.yaw));
                //} break;
                //case MAVLINK_MSG_ID_PARAM_VALUE: {
                //    mavlink_param_value_t param;
                //    mavlink_msg_param_value_decode(&msg, &param);
                //    LogSimple(LOG_DEBUG, "%.16s", param.param_id);
                //} break;
                //default: {
                //    LogSimple(LOG_DEBUG, "MSGID: %d\n", msg.msgid);
                //} break;
            }
            
            //Call the event handler, if any.
            if (msg.msgid >= 0 && msg.msgid < 255) {
                EventHandler e = m_handler_table[msg.msgid];
                if (e) {
                    e(&msg);
                }
            }
        }
        m_last_heartbeat = duration_cast<seconds>(steady_clock::now()-last_heartbeat).count();
        if (m_is_auto_mode && m_last_heartbeat >= m_heartbeat_timeout) {
            Log(LOG_WARNING, "Heartbeat timeout (%d s); disabling auto mode!",
                m_last_heartbeat);
            m_is_auto_mode = false;
        }
    }
}

void FlightBoard::OutputLoop() {
    while (!m_shutdown) {
        if (!m_disable_local && m_is_auto_mode) {
            SetBodyVel(Vec3D{});
        } else {
            //Log(LOG_DEBUG, "SLEEPING");
        }
        sleep_for(milliseconds(100));
    }
}

/**
 * Performs a takeoff.
 * This command will only work if the copter is in guided mode and
 * the motors are already armed. The flightboard will not automatically
 * arm the motors.
 * @param [in] alt Height, in m above ground to takeoff to.
 * @return true iff the message was sent.
 */
bool FlightBoard::DoGuidedTakeoff(int alt) {
    if (m_is_auto_mode && !m_is_in_air && m_is_armed) {
        mavlink_command_long_t cmd = {};
        mavlink_message_t msg;
        
        //Cannot be sending 'set position' messages when taking off!
        m_disable_local = true;
        
        cmd.target_system = m_system_id;
        cmd.target_component = m_component_id;
        cmd.command = MAV_CMD_NAV_TAKEOFF;
        cmd.param7 = std::max(alt, 0);
        
        mavlink_msg_command_long_encode(m_system_id, m_flightboard_id, &msg, &cmd);
        m_link->WriteMessage(&msg);
        return true;
    }
    return false;
}

/**
 * Send a return-to-launch (RTL) failsafe message to the copter.
 * @return true iff the message was sent.
 */
bool FlightBoard::DoReturnToLaunch() {
    mavlink_command_long_t cmd = {};
    mavlink_message_t msg;
    
    Log(LOG_WARNING, "SENDING RETURN TO LAUNCH");
    Stop();
    cmd.command = MAV_CMD_NAV_RETURN_TO_LAUNCH;
    mavlink_msg_command_long_encode(m_system_id, m_flightboard_id, &msg, &cmd);
    m_link->WriteMessage(&msg);
    return true;
}

/**
 * Sets a waypoint to move to.
 * @param [in] seq The sequence id of this waypoint.
 * @param [in] radius Acceptance radius (in m) of being at the waypoint.
 * @param [in] wait The wait time (in s) at the waypoint.
 * @param [in] pt The coordinate of the waypoint.
 * @param [in] relative_alt Whether or not the altitude specified is relative
 *             to the current altitude.
 * @return true iff the waypoint was sent.
 */
bool FlightBoard::SetGuidedWaypoint(int seq, float radius, float wait, navigation::Coord3D pt, bool relative_alt) {
    if (m_is_auto_mode) {
        mavlink_mission_item_t mi = {0};
        mavlink_message_t msg;
        std::lock_guard<std::mutex> lock(m_output_mutex);
        
        mi.target_system = m_system_id;
        mi.target_component = m_component_id;
        mi.seq = seq;
        mi.frame = MAV_FRAME_GLOBAL_RELATIVE_ALT;
        mi.command = MAV_CMD_NAV_WAYPOINT;
        mi.current = 2; //ArduCopter magic number for 'guided mode' waypoint
        mi.param1 = 0; //Hold time in s; we do this ourselves
        mi.param2 = radius; //Acceptance radius in m
        mi.param3 = 0; //Pass through the waypoint; ignored by ArduPilot
        mi.param4 = 0; //Desired yaw angle on completion; ignored by ArduPilot
        mi.x = pt.lat;
        mi.y = pt.lon;
        mi.z = (relative_alt ? (m_gps->GetLatestRelAlt() + pt.alt) : pt.alt);
        
        if (std::isnan(mi.z) || mi.z <= 0) {
            Log(LOG_WARNING, "Waypoint %d: Invalid altitude (%.2f m)", seq, mi.z);
            return false;        
        }
        
        m_disable_local = true;
        mavlink_msg_mission_item_encode(m_system_id, m_flightboard_id, &msg, &mi);
        m_link->WriteMessage(&msg);
        return true;
    }
    return false;
}

/**
 * Changes the maximum speed at which the copter moves to waypoints.
 * @param [in] sp The speed to move at, in m/s.
 * @return true iff the message was sent.
 */
bool FlightBoard::SetWaypointSpeed(int sp) {
    if (m_is_auto_mode) {
        mavlink_command_long_t cmd = {0};
        mavlink_message_t msg;
        //std::lock_guard<std::mutex> lock(m_output_mutex);
        //m_disable_local = true;
        
        cmd.target_system = m_system_id;
        cmd.target_component = m_component_id;
        cmd.command = MAV_CMD_DO_CHANGE_SPEED;
        //All parameters are unused by ArduCopter except this one.
        cmd.param2 = sp;
        
        mavlink_msg_command_long_encode(m_system_id, m_flightboard_id, &msg, &cmd);
        m_link->WriteMessage(&msg);
        return true;
    }
    return false;
}

/**
 * Sets the velocity of the copter, relative to its frame.
 * @param [in] v The velocity vector. NB: Components are limited to +/- 4m/s
 *               for safety reasons.
 * @return true iff the message was sent.
 */
bool FlightBoard::SetBodyVel(Vec3D v) {
    if (m_is_auto_mode) {
        mavlink_message_t msg;
        mavlink_set_position_target_local_ned_t sp = {};
        sp.type_mask = MAVLINK_MSG_SET_POSITION_TARGET_LOCAL_NED_VELOCITY;
        sp.coordinate_frame = MAV_FRAME_BODY_OFFSET_NED;
        sp.target_system = m_system_id;
        sp.target_component = m_component_id;   
       
        sp.vx = picopter::clamp(v.x, -4.0, 4.0);
        sp.vy = picopter::clamp(v.y, -4.0, 4.0);
        sp.vz = picopter::clamp(v.z, -4.0, 4.0);
        
        SetYaw(0, true);
        mavlink_msg_set_position_target_local_ned_encode(m_system_id, m_flightboard_id, &msg, &sp);
        m_link->WriteMessage(&msg);
        return true;
    }
    return false;
}

/**
 * Sets the position of the copter, relative to its frame.
 * @param [in] v The position offset. NB: Components are limited to +/- 10m
 *               for safety reasons.
 * @return true iff the message was sent.
 */
bool FlightBoard::SetBodyPos(Point3D p) {
    if (m_is_auto_mode) {
        mavlink_message_t msg;
        mavlink_set_position_target_local_ned_t sp = {};
        sp.type_mask = MAVLINK_MSG_SET_POSITION_TARGET_LOCAL_NED_POSITION;
        sp.coordinate_frame = MAV_FRAME_BODY_OFFSET_NED;
        sp.target_system = m_system_id;
        sp.target_component = m_component_id;
       
        sp.x = picopter::clamp(p.x, -10.0, 10.0);
        sp.y = picopter::clamp(p.y, -10.0, 10.0);
        sp.z = picopter::clamp(p.z, -10.0, 10.0);
        
        SetYaw(0, true);
        mavlink_msg_set_position_target_local_ned_encode(m_system_id, m_flightboard_id, &msg, &sp);
        m_link->WriteMessage(&msg);
        return true;
    }
    return false;
}

/**
 * Sets the region of interest (faces the copter at the ROI)
 * @param [in] roi The absolute location of the ROI.
 *                 Note: Setting lat=lng=alt=0 will disable ROI tracking.
 * @return true iff the message was sent.
 */
bool FlightBoard::SetRegionOfInterest(Coord3D roi) {
    if (m_is_auto_mode) {
        mavlink_command_long_t cmd = {0};
        mavlink_message_t msg;
        //std::lock_guard<std::mutex> lock(m_output_mutex);
        //m_disable_local = true;
        
        cmd.target_system = m_system_id;
        cmd.target_component = m_component_id;
        cmd.command = MAV_CMD_DO_SET_ROI;
        cmd.param5 = roi.lat;
        cmd.param6 = roi.lon;
        cmd.param7 = roi.alt;
        
        mavlink_msg_command_long_encode(m_system_id, m_flightboard_id, &msg, &cmd);
        m_link->WriteMessage(&msg);
        return true;
    }
    return false;
}

/**
 * Unsets the region of interest. This stops the copter from tracking
 * a particular region of interest. It is a convenience function to calling
 * SetRegionOfInterest with the ROI set to (0,0,0). It also re-enables
 * auto-yaw control.
 * @return true iff the message was sent.
 */
bool FlightBoard::UnsetRegionOfInterest() {
    Coord3D none{};
    return SetRegionOfInterest(none);
}

/**
 * Sets the direction that the copter should be facing.
 * This method disables 'auto yaw'. To re-enable auto-yaw, call
 * UnsetRegionOfInterest.
 * @param [in] bearing The bearing (0-360deg), or offset (deg) if relative.
 * @param [in] relative The 'bearing' should be treated as an offset to
 *                      the current bearing.
 * @return true iff the command was sent.
 */
bool FlightBoard::SetYaw(int bearing, bool relative) {
    if (m_is_auto_mode) {
        mavlink_message_t msg;
        mavlink_command_long_t yaw_sp = {};
        yaw_sp.command = MAV_CMD_CONDITION_YAW;
        yaw_sp.target_system = m_system_id;
        yaw_sp.target_component = m_component_id;
        yaw_sp.param1 = bearing; //Bearing in deg
        yaw_sp.param2 = 0; //Yaw rate in deg/s
        yaw_sp.param3 = 0; //Yaw direction (CCW or CW)
        yaw_sp.param4 = relative ? 1 : 0; //Relative
        mavlink_msg_command_long_encode(m_system_id, m_flightboard_id, &msg, &yaw_sp);
        m_link->WriteMessage(&msg);
        return true;
    }
    return false;
}

/**
 * Determines if the system is in auto mode.
 * This means that the Pixhawk is in guided mode. Where we can send commands. 
 * @return true iff in auto mode. 
 */
bool FlightBoard::IsAutoMode() {
    return m_is_auto_mode;
}

/**
 * Indicates if the copter is in RTL mode.
 * @return true iff in RTL mode.
 */
bool FlightBoard::IsRTL() {
    return m_is_rtl;
}

/**
 * Indicates if the copter is active and in the air (flying)
 * @return true iff active.
 */
bool FlightBoard::IsInAir() {
    return m_is_in_air;
}

/**
 * Indicates if the copter has its motors armed.
 * @return true iff motors are armed.
 */
bool FlightBoard::IsArmed() {
    return m_is_armed;
}

/**
 * Stops the hexacopter.
 * Note: Stopping refers to making it hold its current position.
 * Sets all speeds and the gimbal angle to 0.
 */
void FlightBoard::Stop() {
    std::lock_guard<std::mutex> lock(m_output_mutex);
    m_disable_local = false;
    SetBodyVel(Vec3D{});
}

/**
 * Registers an event handler, which will be called when a message with
 * the given message id is received. The current implementation will only allow
 * up to one function to respond to a specific message at any one time. If a
 * callback function already exists for the given msgid, it will be replaced.
 * A more versatile implementation would maintain some sort of list per msgid
 * to allow for more than one function to be called at any one time. Or, if
 * more than one function really must be called, then a callback function that
 * then itself delegates to these functions can be used instead.
 *  
 * @param [in] msgid The message id to respond to.
 * @param [in] handler The event handler to call.
 * @return -1 on error, or the unique handler id. At present this is just equal
 *         to msgid. 
 */
int FlightBoard::RegisterHandler(int msgid, EventHandler handler) {
    if (msgid < 0 || msgid > 255) {
        return -1;
    } else {
        m_handler_table[msgid] = handler;
        return msgid;
    }
}

/**
 * Deregisters a message handler.
 * @param [in] handlerid The unique handler id as returned from RegisterHandler.
 */
void FlightBoard::DeregisterHandler(int handlerid) {
    if (handlerid >= 0 && handlerid <= 255) {
        m_handler_table[handlerid] = nullptr;
    }
}
