import { AppEventHandler } from "./AppEvent";

/**
 * /dev/null for events.
 */
export default class NoopAppEventHandler implements AppEventHandler {
  // eslint-disable-next-line class-methods-use-this
  handleEvent(): Promise<void> {
    return Promise.resolve();
  }
}
