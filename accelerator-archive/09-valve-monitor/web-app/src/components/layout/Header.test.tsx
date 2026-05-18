import { render, screen } from "@testing-library/react";
import "@testing-library/jest-dom";
import HeaderComponent from "./Header";
import config from "../../../Config";

describe("Header component", () => {
  it("should render the header successfully", () => {
    render(<HeaderComponent />);

    expect(screen.getByText(config.companyName)).toBeInTheDocument();
  });
});
