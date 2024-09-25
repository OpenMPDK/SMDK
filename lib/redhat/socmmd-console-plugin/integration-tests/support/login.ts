declare global {
  namespace Cypress {
    interface Chainable {
      login(username?: string, password?: string): Chainable<Element>;
      logout(): Chainable<Element>;
    }
  }
}

const KUBEADMIN_USERNAME = 'kubeadmin';
const loginUsername = Cypress.env('BRIDGE_KUBEADMIN_PASSWORD') ? 'user-dropdown' : 'username';

// This will add 'cy.login(...)'
// ex: cy.login('my-user', 'my-password')
Cypress.Commands.add('login', (username: string, password: string) => {
  // Check if auth is disabled (for a local development environment).
  cy.visit('/'); // visits baseUrl which is set in plugins/index.js
  cy.window().then((win) => {
    if (win.SERVER_FLAGS?.authDisabled) {
      return;
    }

    // Make sure we clear the cookie in case a previous test failed to logout.
    cy.clearCookie('openshift-session-token');

    cy.get('#inputUsername').type(username || KUBEADMIN_USERNAME);
    cy.get('#inputPassword').type(password || Cypress.env('BRIDGE_KUBEADMIN_PASSWORD'));
    cy.get('button[type=submit]').click();

    cy.get(`[data-test="${loginUsername}"]`).should('be.visible');
  });
});

Cypress.Commands.add('logout', () => {
  // Check if auth is disabled (for a local development environment).
  cy.window().then((win) => {
    if (win.SERVER_FLAGS?.authDisabled) {
      return;
    }
    cy.get('[data-test="user-dropdown"]').click();
    cy.get('[data-test="log-out"]').should('be.visible');
    cy.get('[data-test="log-out"]').click({ force: true });
  });
});
