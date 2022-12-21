/**
 * @jest-environment node
 */
import { StatusCodes } from "http-status-codes";
import type { NextApiRequest, NextApiResponse } from "next";
import { createMocks, RequestMethod } from "node-mocks-http";
import gatewayNameHandler from "../../../../src/pages/api/gateway/[gatewayUID]/name";
import { HTTP_STATUS, HTTP_HEADER } from "../../../../src/constants/http";
import { services } from "../../../../src/services/ServiceLocatorServer";

const authToken = process.env.HUB_AUTH_TOKEN;

function mockRequestResponse(method: RequestMethod = "GET") {
  const { req, res }: { req: NextApiRequest; res: NextApiResponse } =
    createMocks({ method });
  req.headers = {
    [HTTP_HEADER.CONTENT_TYPE]: HTTP_HEADER.CONTENT_TYPE_JSON,
    [HTTP_HEADER.SESSION_TOKEN]: authToken,
  };
  req.query = { gatewayUID: `dev:11111111111` };
  return { req, res };
}

describe("/api/gateways/[gatewayUID]/name API Endpoint", () => {
  it("POST should return a successful response if name can be changed", async () => {
    const app = services().getAppService();
    jest.spyOn(app, "setGatewayName").mockImplementation(async () => {});
    const { req, res } = mockRequestResponse("POST");
    req.body = { name: "TEST_NAME" };
    await gatewayNameHandler(req, res);

    expect(res.statusCode).toBe(200);
    expect(res.getHeaders()).toEqual({
      "content-type": HTTP_HEADER.CONTENT_TYPE_JSON,
    });
    expect(res.statusMessage).toEqual("OK");
  });

  it("POST should return an error if the name can't be changed", async () => {
    const app = services().getAppService();
    jest.spyOn(app, "setGatewayName").mockImplementation(() => {
      throw new Error("boom");
    });

    const { req, res } = mockRequestResponse("POST");
    req.body = { name: "TEST_NAME" };
    const promise = gatewayNameHandler(req, res);

    await expect(promise).rejects.toMatchInlineSnapshot(
      `[ErrorWithCause: could not set gateway name]`
    );
  });

  it("POST should return a 400 if gateway name is invalid", async () => {
    const { req, res } = mockRequestResponse("POST");
    req.body = {}; // no name in body
    await gatewayNameHandler(req, res);

    expect(res.statusCode).toBe(StatusCodes.BAD_REQUEST);
    expect(res.getHeaders()).toEqual({
      "content-type": HTTP_HEADER.CONTENT_TYPE_JSON,
    });
    // eslint-disable-next-line no-underscore-dangle
    expect(res._getJSONData()).toEqual({
      err: HTTP_STATUS.INVALID_GATEWAY_NAME,
    });
  });

  it("POST should return a 400 if GatewayUID is not a string", async () => {
    const { req, res } = mockRequestResponse("POST");
    delete req.query.gatewayUID;
    await gatewayNameHandler(req, res);

    expect(res.statusCode).toBe(StatusCodes.BAD_REQUEST);
    expect(res._getJSONData()).toEqual({
      err: HTTP_STATUS.INVALID_GATEWAY,
    });
  });

  it("should return a 405 if method not POST is passed", async () => {
    const { req, res } = mockRequestResponse("GET");
    req.body = { name: "TEST_NAME" };
    await gatewayNameHandler(req, res);

    expect(res.statusCode).toBe(StatusCodes.METHOD_NOT_ALLOWED);
    // eslint-disable-next-line no-underscore-dangle
    expect(res._getJSONData()).toEqual({ err: "Method GET Not Allowed" });
  });
});
