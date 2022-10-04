/**
 * @jest-environment node
 */
import axios from "axios";
import { changeNodeName } from "../../../src/api-client/node";

describe(changeNodeName, () => {
  it("should construct good request", async () => {
    const spy = jest
      .spyOn(axios, "post")
      .mockImplementation(() => Promise.resolve({ status: 200 }));

    await changeNodeName("gfoo", "sfoo", "bazname");

    expect(spy).toHaveBeenCalledWith("/api/gateway/gfoo/node/sfoo/config", {
      name: "bazname",
    });
  });
});
