const LAST_ALARM_CLEAR_KEY = "LAST_ALARM_CLEAR";

export default class AlarmService {
  // eslint-disable-next-line class-methods-use-this
  getLastAlarmClear() {
    return localStorage.getItem(LAST_ALARM_CLEAR_KEY) || "";
  }

  // eslint-disable-next-line class-methods-use-this
  setLastAlarmClear() {
    const currentTimeInSeconds = Math.floor(Date.now() / 1000);
    localStorage.setItem(LAST_ALARM_CLEAR_KEY, `${currentTimeInSeconds}`);
  }
}
