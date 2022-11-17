import { render, screen, waitFor } from "@testing-library/react";
import "@testing-library/jest-dom/extend-expect";
import userEvent from "@testing-library/user-event";
// eslint-disable-next-line jest/no-mocks-import
import EditInPlace from "./EditInPlace";
import "../../../__mocks__/matchMediaMock"; // needed to avoid error due to JSDOM not implementing method yet: https://jestjs.io/docs/manual-mocks#mocking-methods-which-are-not-implemented-in-jsdom
import { ERROR_MESSAGE } from "../../constants/ui";

describe("EditInPlace handling", () => {
  it("should show an error if a text change fails", async () => {
    render(
      <EditInPlace
        initialText="my-device"
        onChange={() => Promise.resolve(false)}
        errorMessage={ERROR_MESSAGE.DEVICE_NAME_CHANGE_FAILED}
        enabled
      />
    );

    userEvent.click(screen.getByRole("button", { name: /edit/i }));
    userEvent.click(screen.getByRole("button", { name: /check/i }));

    await waitFor(() =>
      screen.getByText(ERROR_MESSAGE.DEVICE_NAME_CHANGE_FAILED)
    );
    expect(
      screen.getByText(ERROR_MESSAGE.DEVICE_NAME_CHANGE_FAILED)
    ).toBeInTheDocument();
  });

  it("should NOT show an error if a name change succeeds", async () => {
    render(
      <EditInPlace
        initialText="my-device"
        onChange={() => Promise.resolve(true)}
        errorMessage={ERROR_MESSAGE.DEVICE_NAME_CHANGE_FAILED}
        enabled
      />
    );

    userEvent.click(screen.getByRole("button", { name: /edit/i }));
    userEvent.click(screen.getByRole("button", { name: /check/i }));

    await waitFor(() => screen.getByRole("button", { name: /edit/i }));
    expect(
      screen.queryByText(ERROR_MESSAGE.DEVICE_NAME_CHANGE_FAILED, {
        exact: false,
      })
    ).not.toBeInTheDocument();
  });

  it("should allow edits when enabled", () => {
    render(
      <EditInPlace
        initialText="my-device"
        onChange={() => Promise.resolve(true)}
        errorMessage=""
        enabled
      />
    );

    expect(screen.getByRole("button", { name: /edit/i }));
  });

  it("should NOT allow edits when disabled", () => {
    render(
      <EditInPlace
        initialText="my-device"
        onChange={() => Promise.resolve(true)}
        errorMessage=""
        enabled={false}
      />
    );

    expect(screen.queryByRole("button", { name: /edit/i })).toBeNull();
  });
});
