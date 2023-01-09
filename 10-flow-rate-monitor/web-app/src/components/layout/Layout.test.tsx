import { render, screen } from "@testing-library/react";
import "@testing-library/jest-dom";
import Layout from "./Layout";

const mockChildren = <p>Hello, I'm a Reference Web App test</p>;

describe("Layout component", () => {
  it("should render the layout successfully", () => {
    render(<Layout children={mockChildren} isLoading={false}></Layout>);

    expect(
      screen.getByText("Hello, I'm a Reference Web App test")
    ).toBeInTheDocument();
  });
});
