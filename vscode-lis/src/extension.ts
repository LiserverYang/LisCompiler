/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 *
 * Lis VSCode extension — launches the lisls language server.
 */

import * as vscode from 'vscode';
import { LanguageClient, LanguageClientOptions, ServerOptions } from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

export function activate(context: vscode.ExtensionContext): void {
    const config = vscode.workspace.getConfiguration('lis-lang');
    let serverCommand: string = config.get<string>('lislsPath', '');
    if (!serverCommand) {
        serverCommand = 'lisls';
    }

    const serverOptions: ServerOptions = { command: serverCommand };
    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'lis' }],
    };

    client = new LanguageClient('lisls', 'Lis Language Server', serverOptions, clientOptions);
    context.subscriptions.push(client);
    client.start().catch((err) => {
        void vscode.window.showErrorMessage(
            `lisls failed to start (check lis-lang.lislsPath): ${err}`);
    });
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}
