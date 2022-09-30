import { render, screen } from "@testing-library/react";
import "@testing-library/jest-dom";
import Layout from "../../../../src/components/layout/Layout";

const mockChildren = <p>Hello, I'm a Sparrow Reference Web App test</p>;

describe("Layout component", () => {
  it("should render the layout successfully", () => {
    render(<Layout>{mockChildren}</Layout>);

    expect(
      screen.getByText("Hello, I'm a Sparrow Reference Web App test")
    ).toBeInTheDocument();
  });
});
