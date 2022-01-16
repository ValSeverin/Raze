class DukeActor : CoreActor native
{ 
	virtual native void Initialize();
	virtual native void Tick();
	virtual native void RunState();	// this is the CON function.
}

class DukeCrane : DukeActor
{
	override native void Initialize();
	override native void Tick();
}

class DukeCranePole : DukeActor
{
}
