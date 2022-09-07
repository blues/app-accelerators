import { render, screen } from "@testing-library/react";
import "@testing-library/jest-dom/extend-expect";
// eslint-disable-next-line jest/no-mocks-import
import "../../../__mocks__/matchMediaMock";
import Form from "./Form";

const mockedFormItems = [
  {
    label: "Another Mocked Label",
    name: "another mocked label",
    tooltip: "What is this tooltip for?",
    initialValue: "This is here for show",
    contents: <div data-testid="form-test-element">Hello App</div>,
  },
];

const mockedOnFinish = jest.fn();

const mockOnFinishFailed = jest.fn();

describe("Form component", () => {
  it("should render a form when form items are supplied", () => {
    render(
      <Form
        formItems={mockedFormItems}
        onFinish={mockedOnFinish}
        onFinishFailed={mockOnFinishFailed}
      />
    );

    expect(screen.getByText(mockedFormItems[0].label)).toBeInTheDocument();
    expect(
      screen.getByTestId("form-test-element", {
        exact: false,
      })
    ).toBeInTheDocument();
  });
});
