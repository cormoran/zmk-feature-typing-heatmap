/** Real browser UI against firmware in Renode via west zmk-web-e2e. */
import { test, expect } from "@playwright/test";
const SHIM_URL = process.env.ZMK_WEB_E2E_SHIM_URL;
const DEVICE_NAME = process.env.ZMK_WEB_E2E_DEVICE_NAME || "Module Test";

test("statistics refresh, reset and persistence use real firmware RPC", async ({
  page,
  request,
}) => {
  test.skip(
    !SHIM_URL,
    "Run through west zmk-web-e2e to provide the firmware DUT."
  );
  await page.addInitScript(await (await request.get(SHIM_URL!)).text());
  await page.goto("/");
  await page.getByRole("button", { name: /Connect USB/ }).click();
  await expect(page.getByText(`Connected to: ${DEVICE_NAME}`)).toBeVisible();
  await expect(
    page.getByRole("heading", { name: "Typing heatmap" })
  ).toBeVisible();
  await expect(page.getByTestId("total-presses")).toBeVisible();
  await expect(page.getByRole("button", { name: "Refresh" })).toBeEnabled();
  await page.getByRole("button", { name: "Refresh" }).click();
  await expect(page.getByRole("button", { name: "Refresh" })).toBeEnabled();
  await expect(page.getByRole("alert")).toHaveCount(0);
  await page.getByRole("button", { name: "Reset statistics" }).click();
  await page.getByRole("button", { name: "Confirm reset" }).click();
  await expect(page.getByTestId("total-presses")).toHaveText("0");
  const persist = page.getByRole("checkbox", {
    name: "Save statistics across restarts",
  });
  await expect(persist).toBeEnabled();
  if (await persist.isChecked()) await persist.click();
  await expect(page.getByText(/Memory only:/)).toBeVisible();
  await expect(persist).not.toBeChecked();
  await persist.click();
  await expect(page.getByText(/Automatic saving requires both/)).toBeVisible();
  await expect(persist).toBeChecked();
  await expect(page.getByRole("alert")).toHaveCount(0);
  const download = page.waitForEvent("download");
  await page.getByRole("button", { name: "Export JSON" }).click();
  expect((await download).suggestedFilename()).toBe("typing-heatmap.json");
  await page.getByRole("button", { name: "Disconnect" }).click();
  await expect(page.getByRole("button", { name: /Connect USB/ })).toBeVisible();
});
