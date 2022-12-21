import { render, screen } from "@testing-library/react";
import "@testing-library/jest-dom";
import FooterComponent from "./Footer";

describe("Footer component", () => {
  it("should render the footer successfully", () => {
    render(<FooterComponent />);

    expect(
      screen.getByText("Cloud-connected by", { exact: false })
    ).toBeInTheDocument();
    expect(
      screen.getByText("Developed by", { exact: false })
    ).toBeInTheDocument();
  });
});
