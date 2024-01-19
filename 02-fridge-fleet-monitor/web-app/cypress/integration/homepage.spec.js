// homepage.spec.js created with Cypress

describe("Fridge Fleet Monitor Application", () => {
  it("should be able to see gateways and nodes on homepage dashboard", function () {
    cy.visit("/");
    // Check logo is visible
    cy.get('[data-testid="logo"]').should("be.visible");
    // Check company name in header is visible
    cy.get('[data-testid="company-name"]').should("be.visible");
    // Check Gateways header is visible
    cy.get('[data-testid="gateway-header"]').should("contain", "Gateway");
    // Check Nodes header is visible
    cy.get('[data-testid="node-header"]').should("contain", "Nodes");
    // Check footer elements are visible
    cy.get('[data-testid="notecard-link"]').should("be.visible");
    cy.get('[data-testid="blues-link"]').should("be.visible");
    // Check footer links are correct
    cy.get('[data-testid="notecard-link"]')
      .should("have.attr", "href")
      .and("include", "https://blues.com/products");
    cy.get('[data-testid="blues-link"]')
      .should("have.attr", "href")
      .and("include", "https://blues.com");
  });

  it("should be able to click on a gateway UID and see the details of that gateway and its related nodes", function () {
    cy.visit("/");
    //Click the Gateway Details arrow
    cy.clickGatewayCard("0");
    //Verify the Gateway Details header
    cy.get('[data-testid="gateway-details-header"]').should(
      "contain",
      "Gateway"
    );
    // check for gateway details
    cy.get(".ant-card-body").should("contain", "Location");
    cy.get('[data-testid="gateway-location"]', { timeout: 90000 }).should(
      "be.visible"
    );
    cy.get('[data-testid="gateway-last-seen"]').should("contain", "Last seen");
    // check for nodes related to gateway
    cy.get('[data-testid="gateway-node-header"]').should("contain", "Nodes");
    // check node details
    cy.get('[data-testid="node[0]-summary"]').should("be.visible");
    cy.get('[data-testid="node-timestamp"]').should("be.visible");
    cy.get('[data-testid="node-location"]').should("be.visible");
    cy.get(".ant-card-body :nth-child(1)").should("contain", "Humidity");
    cy.get(".ant-card-body :nth-child(2)").should("contain", "Pressure");
    cy.get(".ant-card-body :nth-child(3)").should("contain", "Temperature");
    cy.get(".ant-card-body :nth-child(4)").should("contain", "Voltage");
    cy.get(".ant-card-body :nth-child(5)").should("contain", "Door Status");
    //Click the logo to return to the homepage
    cy.get('[data-testid="logo"]').click({ force: true });
    // verify it navigates back to the homepage
    cy.get('[data-testid="gateway-header"]', { timeout: 20000 }).should(
      "be.visible"
    );
  });

  it("should be able to click on a node card and see more details about that node and update the name and location of that node", function () {
    // this keeps uncaught exceptions from failing Cypress tests
    Cypress.on("uncaught:exception", (err, runnable) => {
      return false;
    });

    cy.visit("/");
    //Click the first Node card
    cy.clickNodeCard("0");
    //TODO: remove once the page architecture is fixed
    //wait for the (very slow) page to load
    //Verify the Node Name header
    cy.get('[data-testid="node-name"]', { timeout: 90000 }).should(
      "be.visible"
    );
    // Verify the parent gateway name is displayed
    cy.get('[data-testid="node-gateway-name"]').should("be.visible");
    //Verify the Current Readings header
    cy.get('[data-testid="current-readings"]').should(
      "contain",
      "Current Readings"
    );
    //Verify the Last Seen header
    cy.get('[data-testid="last-seen"]').should("contain", "Last updated");
    //Verify the Temperature header
    cy.get('[data-testid="temperature"]').should("contain", "Temperature");
    //Verify the Humidity header
    cy.get('[data-testid="humidity"]').should("contain", "Humidity");
    //Verify the Voltage header
    cy.get('[data-testid="voltage"]').should("contain", "Voltage");
    //Verify the Pressure header
    cy.get('[data-testid="pressure"]').should("contain", "Pressure");
    //Verify the Door Status header
    cy.get('[data-testid="door-status"]').should("contain", "Door Status");
    //Click the Details tab
    cy.clickTabByText("Settings");
    //Check for the Name label
    cy.get(".ant-form-item-required").should("contain", "Name");
    //Verify the Name field exists in the Details tab
    const nodeNameInput = cy.get('[data-testid="form-input-node-name"]', {
      timeout: 15000,
    });
    nodeNameInput.should("be.visible");
    // Enter a new node name
    nodeNameInput.clear().type("Cypress Test Node");
    //Check for the location label
    cy.get(".ant-form-item-required").should("contain", "Location");
    //Verify the Location field exists in the Details tab
    const nodeLocationInput = cy.get(
      '[data-testid="form-input-node-location"]'
    );
    nodeLocationInput.should("be.visible");
    // Enter a new node location
    nodeLocationInput.clear().type("Cypress Runner");
    //Click the Submit button
    const nodeSubmitButton = cy.get('[data-testid="form-submit"]');
    nodeSubmitButton.should("be.visible");
    cy.get(".ant-form").submit();
    // Verify the node name is now updated to "Cypress Test Node"
    cy.get('[data-testid="node-name"]').should("contain", "Cypress Test Node");
    // Enter a second new node name
    cy.get('[data-testid="form-input-node-name"]')
      .clear()
      .type("Other Node Name");
    // Enter a second new node location
    cy.get('[data-testid="form-input-node-location"]').clear().type("Garage");
    //Click the Submit button
    cy.get(".ant-form").submit();
    // Verify the node name is now updated to "Other Node Name"
    cy.get('[data-testid="node-name"]').should("contain", "Other Node Name");
  });

  it.skip("should be able to paginate through the carousel for multiple gateways", function () {
    cy.visit("/");
    // Check first gateway card is visible
    cy.get('[data-testid="gateway[0]-details"]', { timeout: 50000 }).should(
      "be.visible"
    );
    // check 2nd gateway card is NOT visible
    cy.get('[data-testid="gateway[1]-details"]').should("not.be.visible");
    // click carousel button
    cy.clickCarouselButton("right");
    // check 1st gateway card is NOT visible
    cy.get('[data-testid="gateway[0]-details"]', { timeout: 10000 }).should(
      "not.be.visible"
    );
  });

  it("should allow you to change a gateway’s name", function () {
    cy.visit("/");
    cy.clickGatewayCard("0");
    cy.get('[data-testid="edit-in-place-edit-button"]').click();
    cy.get("#name").clear();
    cy.get("#name").type("CYPRESS_TEST");
    cy.get('[data-testid="edit-in-place-submit-button"]').click();
    cy.get('[data-testid="gateway-details-header"]').should(
      "contain",
      "CYPRESS_TEST"
    );
  });
});
