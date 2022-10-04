import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import "@testing-library/jest-dom/extend-expect";
// eslint-disable-next-line jest/no-mocks-import
import "../../../../__mocks__/matchMediaMock";
import GatewayCard from "../../../../src/components/elements/GatewayCard";
import { GATEWAY_MESSAGE } from "../../../../src/constants/ui";

const mockGatewayData = {
  uid: "My Mocked Gateway",
  name: "67890",
  location: "Gainesville, FL",
  lastActivity: "2022-01-05T07:36:55Z",
  nodeList: [
    {
      name: "My First Mocked Node",
      nodeId: "1011",
      humidity: 29,
      pressure: 1000,
      temperature: 24.5,
      voltage: 4.2,
      doorStatus: "CLOSED",
      lastActivity: "2022-01-07T15:28:38Z",
      gatewayUID: "My Mocked Gateway",
    },
  ],
};

const mockedGatewayDataLongName = {
  uid: "Another Mocked Gateway",
  name: "My Mocked Gateway With an Unbelievably Long Serial Number, Seriously You'll be Amazed",
  location: "San Diego, CA",
  lastActivity: "2022-02-11T08:48:01Z",
  nodeList: [],
};

const mockUndefinedGatewayData = {
  uid: "My Other Mocked Gateway",
  name: "13579",
  lastActivity: "2022-01-07T09:12:00Z",
  nodeList: [],
};

const index = 1;

describe("Gateway card component", () => {
  it("should render the card when gateway data is supplied", () => {
    render(<GatewayCard gatewayDetails={mockGatewayData} index={index} />);

    expect(screen.getByText(mockGatewayData.name)).toBeInTheDocument();
    expect(
      screen.getByText(mockGatewayData.location, {
        exact: false,
      })
    ).toBeInTheDocument();
  });

  it("should render the card when particular gateway data is missing", () => {
    render(
      <GatewayCard gatewayDetails={mockUndefinedGatewayData} index={index} />
    );

    expect(screen.getByText(mockUndefinedGatewayData.name)).toBeInTheDocument();
    expect(
      screen.getByText(GATEWAY_MESSAGE.NO_LOCATION, { exact: false })
    ).toBeInTheDocument();
  });

  it("should add an ellipsis and provide a tooltip when gateway data name is too long to fit on card", () => {
    render(
      <GatewayCard gatewayDetails={mockedGatewayDataLongName} index={index} />
    );
    userEvent.hover(
      screen.getByText(mockedGatewayDataLongName.name, { exact: false })
    );
    expect(
      screen.getByText(mockedGatewayDataLongName.name)
    ).toBeInTheDocument();
  });
});
