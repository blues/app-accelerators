import { RightOutlined } from "@ant-design/icons";
import type { CustomArrowProps } from "react-slick";

// required to stop React errors that currentSlide and slideCount must be lowercase
// this is a known issue with the underlying react-slick library the Ant D carousel relies upon
// https://github.com/akiran/react-slick/issues/1195
const CarouselArrowFixRight = ({
  currentSlide,
  slideCount,
  ...props
}: CustomArrowProps) => (
  // eslint-disable-next-line react/jsx-props-no-spreading
  <span data-testid="right-carousel-arrow" {...props}>
    <RightOutlined />
  </span>
);

export default CarouselArrowFixRight;
