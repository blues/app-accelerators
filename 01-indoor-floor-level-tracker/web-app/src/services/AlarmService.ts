import { DeviceTracker } from "./AppModel";

const LAST_ALARM_CLEAR_KEY = "LAST_ALARM_CLEAR";

export default class AlarmService {
  // eslint-disable-next-line class-methods-use-this
  getLastAlarmClear() {
    return localStorage.getItem(LAST_ALARM_CLEAR_KEY) || "";
  }

  areAlarmsPresent(trackers?: DeviceTracker[]) {
    const lastAlarmClear = this.getLastAlarmClear();
    let isAlarmPresent = false;
    trackers?.forEach((tracker) => {
      if (
        tracker.lastAlarm &&
        parseInt(tracker.lastAlarm, 10) > parseInt(lastAlarmClear, 10)
      ) {
        isAlarmPresent = true;
      }
    });
    return isAlarmPresent;
  }

  // eslint-disable-next-line class-methods-use-this
  setLastAlarmClear() {
    const currentTimeInSeconds = Math.floor(Date.now() / 1000);
    localStorage.setItem(LAST_ALARM_CLEAR_KEY, `${currentTimeInSeconds}`);
  }
}
