/**
 * @jest-environment node
 */
import { StatusCodes } from "http-status-codes";
import type { NextApiRequest, NextApiResponse } from "next";
import { createMocks, RequestMethod } from "node-mocks-http";
import { HTTP_STATUS, HTTP_HEADER } from "../../../../constants/http";
import { services } from "../../../../services/ServiceLocatorServer";
import deviceNameHandler from "./name";

const authToken = process.env.HUB_AUTH_TOKEN;

function mockRequestResponse(method: RequestMethod = "GET") {
  const { req, res }: { req: NextApiRequest; res: NextApiResponse } =
    createMocks({ method });
  req.headers = {
    [HTTP_HEADER.CONTENT_TYPE]: HTTP_HEADER.CONTENT_TYPE_JSON,
    [HTTP_HEADER.SESSION_TOKEN]: authToken,
  };
  req.query = { deviceUID: `dev:11111111111` };
  return { req, res };
}

describe("/api/device/[deviceUID]/name API Endpoint", () => {
  it("POST should return a successful response if name can be changed", async () => {
    const app = services().getAppService();
    jest.spyOn(app, "setDeviceName").mockImplementation(async () => {});
    const { req, res } = mockRequestResponse("POST");
    req.body = { name: "TEST_NAME" };
    await deviceNameHandler(req, res);

    expect(res.statusCode).toBe(200);
    expect(res.getHeaders()).toEqual({
      "content-type": HTTP_HEADER.CONTENT_TYPE_JSON,
    });
    expect(res.statusMessage).toEqual("OK");
  });

  it("POST should return an error if the name can't be changed", async () => {
    const app = services().getAppService();
    jest.spyOn(app, "setDeviceName").mockImplementation(() => {
      throw new Error("boom");
    });

    const { req, res } = mockRequestResponse("POST");
    req.body = { name: "TEST_NAME" };
    const promise = deviceNameHandler(req, res);

    await expect(promise).rejects.toMatchInlineSnapshot(
      `[ErrorWithCause: could not set device name]`
    );
  });

  it("POST should return a 400 if device name is invalid", async () => {
    const { req, res } = mockRequestResponse("POST");
    req.body = {}; // no name in body
    await deviceNameHandler(req, res);

    expect(res.statusCode).toBe(StatusCodes.BAD_REQUEST);
    expect(res.getHeaders()).toEqual({
      "content-type": HTTP_HEADER.CONTENT_TYPE_JSON,
    });
    // eslint-disable-next-line no-underscore-dangle
    expect(res._getJSONData()).toEqual({
      err: HTTP_STATUS.INVALID_DEVICE_NAME,
    });
  });

  it("POST should return a 400 if deviceUID is not a string", async () => {
    const { req, res } = mockRequestResponse("POST");
    delete req.query.deviceUID;
    await deviceNameHandler(req, res);

    expect(res.statusCode).toBe(StatusCodes.BAD_REQUEST);
    expect(res._getJSONData()).toEqual({
      err: HTTP_STATUS.INVALID_DEVICE,
    });
  });

  it("should return a 405 if method not POST is passed", async () => {
    const { req, res } = mockRequestResponse("GET");
    req.body = { name: "TEST_NAME" };
    await deviceNameHandler(req, res);

    expect(res.statusCode).toBe(StatusCodes.METHOD_NOT_ALLOWED);
    // eslint-disable-next-line no-underscore-dangle
    expect(res._getJSONData()).toEqual({ err: "Method GET Not Allowed" });
  });
});
