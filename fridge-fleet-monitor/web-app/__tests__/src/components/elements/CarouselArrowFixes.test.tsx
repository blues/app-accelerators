/* eslint-disable react/jsx-props-no-spreading */
import { render, screen } from "@testing-library/react";
import "@testing-library/jest-dom/extend-expect";
import CarouselArrowFixLeft from "../../../../src/components/elements/CarouselArrowFixLeft";
import CarouselArrowFixRight from "../../../../src/components/elements/CarouselArrowFixRight";

describe("Left carousel arrow component", () => {
  it("should render the left carousel arrow", () => {
    render(<CarouselArrowFixLeft />);
    expect(screen.getByTestId("left-carousel-arrow")).toBeInTheDocument();
  });
});

describe("Right carousel arrow component", () => {
  it("should render the right carousel arrow", () => {
    render(<CarouselArrowFixRight />);
    expect(screen.getByTestId("right-carousel-arrow")).toBeInTheDocument();
  });
});
