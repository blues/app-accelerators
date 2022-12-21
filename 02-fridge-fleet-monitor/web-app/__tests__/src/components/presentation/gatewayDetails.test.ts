import Gateway from "../../../../src/services/alpha-models/Gateway";
import { getGatewayDetailsPresentation } from "../../../../src/components/presentation/gatewayDetails";
import { GATEWAY_MESSAGE } from "../../../../src/constants/ui";

function getMockGateway(): Gateway {
  return {
    uid: "dev:123",
    name: "my-gateway",
    lastActivity: "2022-01-13T15:02:46Z",
    location: "someplace",
    voltage: 1.2,
    nodeList: [],
  };
}

describe("Gateway details presentation handling", () => {
  it("should render a gateway location if one is present", () => {
    const gateway = getMockGateway();
    gateway.location = "Michigan";
    const viewModel = getGatewayDetailsPresentation(gateway);
    expect(viewModel.gateway?.location).toBe(gateway.location);
  });

  it("should not render a gateway location if one is not present", () => {
    const gateway = getMockGateway();
    delete gateway.location;
    const viewModel = getGatewayDetailsPresentation(gateway);
    expect(viewModel.gateway?.location).toBe(GATEWAY_MESSAGE.NO_LOCATION);
  });
});
