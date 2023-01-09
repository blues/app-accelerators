import { AppEvent, AppEventHandler } from "./AppEvent";

export class CompositeEventHandler implements AppEventHandler {
  constructor(private readonly handlers: AppEventHandler[]) {}

  async handleEvent(
    event: AppEvent,
    isHistorical?: boolean | undefined
  ): Promise<void> {
    await Promise.all(
      this.handlers.map((e) => e.handleEvent(event, isHistorical))
    );
  }
}
