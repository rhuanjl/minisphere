/**
 *  Sphere Runtime for Sphere games
 *  Copyright (c) 2015-2017, Fat Cerberus
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither the name of miniSphere nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
**/

import from from 'from';
import FocusTarget from 'focus-target';
import Pact from 'pact';

let currentSelf = null;

export default
class Thread
{
	get [Symbol.toStringTag]() { return 'Thread'; }

	static create(entity, priority)
	{
		let thread = new PromptThread(entity, priority);
		thread.start();
		return thread;
	}

	static join(...threads)
	{
		let promises = from.array(threads)
			.select(it => it._pact.promise);
		return Promise.all(promises);
	}

	static self()
	{
		return currentSelf;
	}

	constructor(options = {})
	{
		if (new.target === Thread)
			throw new Error(`'${new.target.name}' is abstract and cannot be instantiated`);

		options = Object.assign({}, {
			inBackground: false,
			priority:     0.0,
		}, options);

		this._bootstrapping = false;
		this._busy = false;
		this._focusTarget = new FocusTarget(options);
		this._inBackground = options.inBackground;
		this._pact = Pact.resolve();
		this._priority = options.priority;
		this._renderJob = null;
		this._started = false;
		this._updateJob = null;
	}

	get hasFocus()
	{
		return this._focusTarget.hasFocus;
	}

	get priority()
	{
		return this._priority;
	}

	get running()
	{
		return this._started;
	}

	on_inputCheck() {}
	on_render() {}
	on_startUp() {}
	on_update() {}

	start()
	{
		if (this._started)
			return;

		this._bootstrapping = true;
		this._started = true;
		this._pact = new Pact();

		this._renderJob = Dispatch.onRender(() => {
			this.on_render();
		}, {
			inBackground: this._inBackground,
			priority:     this._priority,
		});

		this._updateJob = Dispatch.onUpdate(async () => {
			if (this._busy)
				return;
			let lastSelf = currentSelf;
			currentSelf = this;
			this._busy = true;
			if (this._bootstrapping) {
				this._bootstrapping = false;
				await this.on_startUp();
			}
			if (this.hasFocus)
				await this.on_inputCheck();
			await this.on_update();
			this._busy = false;
			currentSelf = lastSelf;
		}, {
			inBackground: this._inBackground,
			priority:     this._priority,
		});

		// after thread terminates, remove it from the input queue
		this._pact.promise.then(() => {
			this._focusTarget.dispose();
		});
	}

	stop()
	{
		if (!this._started)
			return;

		this.yieldFocus();
		this._updateJob.cancel();
		this._renderJob.cancel();
		this._started = false;
		this._pact.resolve();
	}

	takeFocus()
	{
		if (!this.running)
			throw new Error("thread is not running");
		if (this.on_inputCheck === Thread.on_inputCheck)
			throw new TypeError("thread not enabled for user input");

		this._focusTarget.takeFocus();
	}

	yieldFocus()
	{
		if (!this.running)
			throw new Error("thread is not running");
		if (this.on_inputCheck === Thread.on_inputCheck)
			throw new TypeError("thread not enabled for user input");

		this._focusTarget.yield();
	}
}

class PromptThread extends Thread
{
	constructor(entity, priority = 0.0)
	{
		super({ priority });

		this.entity = entity;
	}

	async on_inputCheck()
	{
		if (this.entity.getInput !== undefined)
			await this.entity.getInput();
	}

	on_render()
	{
		if (this.entity.render !== undefined)
			return this.entity.render();
	}

	async on_update()
	{
		if (this.entity.update === undefined || !await this.entity.update())
			this.stop();
	}
}
