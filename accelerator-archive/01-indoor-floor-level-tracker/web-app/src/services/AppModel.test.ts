import * as AppModel from "./AppModel";

import IDBuilder from "./IDBuilder";

const mockProjectUID = "app:12345";

function serialize(obj: unknown): unknown {
  const jsonString = JSON.stringify(obj);
  const result = JSON.parse(jsonString);
  return result;
}

function assertSerializable(sut: unknown) {
  expect(serialize(sut)).toEqual(sut);
}

describe("AppModel serialization", () => {
  describe("projectID", () => {
    it("is serializable", () => {
      const sut = IDBuilder.buildProjectID(mockProjectUID);
      assertSerializable(sut);
    });
  });
});
