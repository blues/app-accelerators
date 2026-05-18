/**
 * @jest-environment node
 */
import { NextJsUrlManager } from "./NextJsUrlManager";

/* eslint-disable jest/valid-title */
/* eslint-disable @typescript-eslint/unbound-method */

describe("NextJsUrlManager", () => {
  describe(NextJsUrlManager.presentNotifications, () => {
    it("returns the base API endpoint when no ids are present", () => {
      expect(NextJsUrlManager.presentNotifications()).toMatchInlineSnapshot(
        `"/api/notifications?format=app"`
      );
    });

    it("returns a single ID as an 'id' query parameter", () => {
      expect(NextJsUrlManager.presentNotifications("foo")).toMatchInlineSnapshot(
        `"/api/notifications?format=app&id=foo"`
      );
    });

    it("returns multiple ID as multiple 'id' query parameters", () => {
      expect(
        NextJsUrlManager.presentNotifications("foo", "bar", "baz")
      ).toMatchInlineSnapshot(`"/api/notifications?format=app&id=foo&id=bar&id=baz"`);
    });
  });

  describe(NextJsUrlManager.notifications, () => {
    it("returns the base API endpoint when no ids are present", () => {
      expect(NextJsUrlManager.notifications()).toMatchInlineSnapshot(
        `"/api/notifications"`
      );
    });

    it("returns a signle ID as an 'id' query parameter", () => {
      expect(NextJsUrlManager.notifications("foo")).toMatchInlineSnapshot(
        `"/api/notifications?id=foo"`
      );
    });

    it("returns multiple ID as multiple 'id' query parameters", () => {
      expect(
        NextJsUrlManager.notifications("foo", "bar", "baz")
      ).toMatchInlineSnapshot(`"/api/notifications?id=foo&id=bar&id=baz"`);
    });
  })});
