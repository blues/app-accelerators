import { cleanup, render, screen } from "@testing-library/react";
import "@testing-library/jest-dom";
import Layout from "./Layout";

const mockChildren = <p>Hello, I'm an Indoor Floor-Level Tracker test</p>;

describe("Layout component", () => {
  it("should render the layout successfully", async () => {
    const useRouter = jest.spyOn(require("next/router"), "useRouter");
    useRouter.mockReturnValue({
      asPath: "",
    });

    render(<Layout children={mockChildren} isLoading={false} />);

    expect(useRouter).toHaveBeenCalled();
    expect(
      screen.getByText("Hello, I'm an Indoor Floor-Level Tracker test")
    ).toBeInTheDocument();

    await cleanup(); // workaround https://github.com/ant-design/ant-design/issues/30964
  });
});
