import "@testing-library/jest-dom/vitest"

/*
 * Runs before every test file, including the node-environment ones, so it must
 * not assume a DOM exists. Per-file jsdom setup belongs in the file that opts
 * into jsdom.
 */
