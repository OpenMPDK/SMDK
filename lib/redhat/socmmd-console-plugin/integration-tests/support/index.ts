// Import commands.js using ES2015 syntax:
import './login';

export const checkErrors = () =>
  cy.window().then((win) => {
    assert.isTrue(!win.windowError, win.windowError);
  });
