import { SparrowEventHandler } from "./SparrowEvent";

/**
 * /dev/null for events.
 */
export default class NoopSparrowEventHandler implements SparrowEventHandler {
  // eslint-disable-next-line class-methods-use-this
  handleEvent(): Promise<void> {
    return Promise.resolve();
  }
}
