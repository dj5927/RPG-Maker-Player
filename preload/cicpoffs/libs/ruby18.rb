# encoding: UTF-8
# Ruby 1.8 behavior compatibility for scripts executed on newer Ruby runtimes.
unless RUBY_VERSION.to_s.index('1.8') == 0
  class Object
    alias type class unless method_defined?(:type)
  end
  class Array
    def nitems; inject(0) { |n, x| x.nil? ? n : n + 1 }; end unless method_defined?(:nitems)
    def choice; self[rand(size)]; end unless method_defined?(:choice)
  end
  class Hash
    def index(value); key(value); end unless method_defined?(:index)
  end
end
