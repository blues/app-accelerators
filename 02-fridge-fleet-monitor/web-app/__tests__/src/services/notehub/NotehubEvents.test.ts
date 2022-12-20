import {
  normalizeSparrowEvent,
  NormalizedEventName,
} from "../../../../src/services/notehub/SparrowEvents";

describe("NotehubEvents", () => {
  describe("Given a plain event name", () => {
    const eventName = "plain-event.qo";

    describe("calling NotehubEvents.normalizeSparrowEvent()", () => {
      let result: NormalizedEventName;

      beforeEach(() => {
        result = normalizeSparrowEvent(eventName);
      });

      it("returns undefined for the nodeID", () => {
        expect(result.nodeID).toBeUndefined();
      });

      it("returns the event name unmodifed", () => {
        expect(result.eventName).toBe(eventName);
      });
    });
  });

  describe("Given a Sparrow node encoded event name", () => {
    const normalizedEventName = "plain-event.qo";
    const basicEventName = "plain-event.qo";
    const nodeID = "AABBCCDDEEFFGG00";

    const eventName = `${nodeID}#${basicEventName}`;

    describe("calling NotehubEvents.normalizeSparrowEvent()", () => {
      let result: NormalizedEventName;

      beforeEach(() => {
        result = normalizeSparrowEvent(eventName);
      });

      it("returns the nodeID", () => {
        expect(result.nodeID).toBe(nodeID);
      });

      it("returns the normalized event name", () => {
        expect(result.eventName).toBe(normalizedEventName);
      });
    });
  });

  describe("Given a notefile event with a node", () => {
    const nodeID = "AABBCCDDEEFFGG00";

    const eventName = "plain-event.qo";

    describe("calling NotehubEvents.normalizeSparrowEvent()", () => {
      let result: NormalizedEventName;

      beforeEach(() => {
        result = normalizeSparrowEvent(eventName, nodeID);
      });

      it("returns the nodeID", () => {
        expect(result.nodeID).toBe(nodeID);
      });

      it("returns the original event name", () => {
        expect(result.eventName).toBe(eventName);
      });
    });
  });
});
