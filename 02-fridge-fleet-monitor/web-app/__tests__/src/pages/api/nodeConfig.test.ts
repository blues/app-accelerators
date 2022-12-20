/**
 * @jest-environment node
 */
import { createMocks, RequestMethod } from "node-mocks-http";
import type { NextApiRequest, NextApiResponse } from "next";
import { StatusCodes } from "http-status-codes";
import nodeConfigHandler from "../../../../src/pages/api/gateway/[gatewayUID]/node/[nodeId]/config";
import { HTTP_STATUS, HTTP_HEADER } from "../../../../src/constants/http";
import { services } from "../../../../src/services/ServiceLocatorServer";
import TestConfig from "../../TestConfig";

describe("/api/gateway/[gatewayUID]/node/[nodeId]/config API Endpoint", () => {
  const authToken = process.env.HUB_AUTH_TOKEN;
  const nodeId = TestConfig.testNodeId;

  function mockRequestResponse(method: RequestMethod = "GET") {
    const { req, res }: { req: NextApiRequest; res: NextApiResponse } =
      createMocks({ method });
    req.headers = {
      [HTTP_HEADER.CONTENT_TYPE]: HTTP_HEADER.CONTENT_TYPE_JSON,
      [HTTP_HEADER.SESSION_TOKEN]: authToken,
    };
    req.query = { gatewayUID: "dev:11111", nodeId };
    return { req, res };
  }

  it("POST should return a successful response if the node can be changed", async () => {
    const app = services().getAppService();
    jest.spyOn(app, "setNodeLocation").mockImplementation(async () => {});
    jest.spyOn(app, "setNodeName").mockImplementation(async () => {});
    const { req, res } = mockRequestResponse("POST");
    req.body = { location: "TEST_LOCATION", name: "TEST_NAME" };
    await nodeConfigHandler(req, res);

    expect(res.statusCode).toBe(StatusCodes.OK);
    expect(res.getHeaders()).toEqual({
      "content-type": HTTP_HEADER.CONTENT_TYPE_JSON,
    });
    expect(res.statusMessage).toEqual("OK");
  });

  it("POST should return an error if things can't be changed", async () => {
    const app = services().getAppService();
    jest.spyOn(app, "setNodeLocation").mockImplementation(() => {
      throw new Error("ugh");
    });

    const { req, res } = mockRequestResponse("POST");
    req.body = { location: "TEST_LOCATION" };
    const promise = nodeConfigHandler(req, res);

    await expect(promise).rejects.toMatchInlineSnapshot(
      `[ErrorWithCause: could not handle node config]`
    );
  });

  it("POST should return a 400 if Node config is invalid", async () => {
    const { req, res } = mockRequestResponse("POST");
    req.body = {}; // no name and no location given. bad.
    await nodeConfigHandler(req, res);

    expect(res.statusCode).toBe(StatusCodes.BAD_REQUEST);
    expect(res.getHeaders()).toEqual({
      "content-type": HTTP_HEADER.CONTENT_TYPE_JSON,
    });
    // eslint-disable-next-line no-underscore-dangle
    expect(res._getJSONData()).toEqual({
      err: HTTP_STATUS.INVALID_NODE_CONFIG_BODY,
    });
  });

  it("POST should return a BAD_REQUEST if Node name is not a string", async () => {
    const { req, res } = mockRequestResponse("POST");
    req.body = { name: 40 };
    await nodeConfigHandler(req, res);

    expect(res.statusCode).toBe(StatusCodes.BAD_REQUEST);
    expect(res._getJSONData()).toEqual({
      err: "name should be a string or undefined",
    });
  });

  it("POST should return a BAD_REQUEST if Node location is not a string", async () => {
    const { req, res } = mockRequestResponse("POST");
    req.body = { location: 40 };
    await nodeConfigHandler(req, res);

    expect(res.statusCode).toBe(StatusCodes.BAD_REQUEST);
    expect(res._getJSONData()).toEqual({
      err: "location should be a string or undefined",
    });
  });

  it("should return a 400 if Gateway UID is not a string", async () => {
    const { req, res } = mockRequestResponse("POST");
    req.query.gatewayUID = 11; // Pass gateway UID of the incorrect type

    await nodeConfigHandler(req, res);

    expect(res.statusCode).toBe(StatusCodes.BAD_REQUEST);
    // eslint-disable-next-line no-underscore-dangle
    expect(res._getJSONData()).toEqual({ err: HTTP_STATUS.INVALID_GATEWAY });
  });

  it("should return a 405 if method not POST is passed", async () => {
    const { req, res } = mockRequestResponse("PUT");
    req.body = { location: "FAILING_TEST_LOCATION", name: "FAILING_TEST_NAME" };
    await nodeConfigHandler(req, res);

    expect(res.statusCode).toBe(StatusCodes.METHOD_NOT_ALLOWED);
    // eslint-disable-next-line no-underscore-dangle
    expect(res._getJSONData()).toEqual({ err: "Method PUT Not Allowed" });
  });
});
