// eslint-disable-next-line @typescript-eslint/no-var-requires, no-undef
const { CustomJSONLexer } = require('./i18n-scripts/lexers');

// eslint-disable-next-line no-undef
module.exports = {
  sort: true,
  createOldCatalogs: false,
  keySeparator: false,
  locales: ['en','ko'],
  namespaceSeparator: '~',
  reactNamespace: false,
  defaultNamespace: 'plugin__socmmd-console-plugin',
  useKeysAsDefaultValue: true,

  // see below for more details
  lexers: {
    hbs: ['HandlebarsLexer'],
    handlebars: ['HandlebarsLexer'],

    htm: ['HTMLLexer'],
    html: ['HTMLLexer'],

    mjs: ['JavascriptLexer'],
    js: ['JavascriptLexer'], // if you're writing jsx inside .js files, change this to JsxLexer
    ts: ['JavascriptLexer'],
    jsx: ['JsxLexer'],
    tsx: ['JsxLexer'],
    json: [CustomJSONLexer],

    default: ['JavascriptLexer'],
  },
};
