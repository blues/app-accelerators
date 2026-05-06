// necessary new setup file required to fix bug where the Nextjs Image doesn't play well with SVG image mocking in Jest: https://github.com/vercel/next.js/issues/26749
jest.mock("next/image", () => ({
  __esModule: true,
  default: () => "Next image stub", // whatever
}));
