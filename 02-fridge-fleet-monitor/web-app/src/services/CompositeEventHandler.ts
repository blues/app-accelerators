import { SparrowEvent, SparrowEventHandler } from "./SparrowEvent";

export class CompositeEventHandler implements SparrowEventHandler {
  constructor(private readonly handlers: SparrowEventHandler[]) {}

  async handleEvent(
    event: SparrowEvent,
    isHistorical?: boolean | undefined
  ): Promise<void> {
    await Promise.all(
      this.handlers.map((e) => e.handleEvent(event, isHistorical))
    );
  }
}
